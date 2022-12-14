/*
 TALLY MASTER

 Firmware per gestionar un sistema de Tally's inhalambrics.

 */
/*
TODO

ELIMINAR LES SIMULACIONS DE CONFIRMACIO DE QL!!!
Linees: 497... i 526...

Poder selecionar el WIFI LOCAL
WEb display per veure tally operatius, bateries i funcions
Fer menu selecció funció local
Implentar Display
Implentar hora
Implentar mostrar texte
Lectura valors reals bateria

*/
/*
  Basat en:

https://randomnerdtutorials.com/esp-now-auto-pairing-esp32-esp8266/

  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/?s=esp-now
  Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.
  The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
  Based on JC Servaye example: https://github.com/Servayejc/esp_now_web_server/
*/
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h> //Control neopixels
#include "PCF8575.h"           //Expansió I2C GPIO
#include <LiquidCrystal_I2C.h> //Control display cristall liquid

#define VERSIO "M1.1" // Versió del software

// Bool per veure missatges de debug
bool debug = true;

// Set your Board and Server ID
#define BOARD_ID 0 // Cal definir cada placa amb el seu numero
// TODO Poder definir en el menu el numero
#define MAX_CHANNEL 13 // for North America // 13 in Europe

// Configurem LED BUILTIN
#ifndef LED_BUILTIN
#define LED_BUILTIN 13 // definim el LED local de la placa
#endif

// Definim equips externs
// TODO: En el menú de coniguració he de poder configurar quin equip tinc conectat a cada port

// TODO: Autodetecció equip:
// Si Input6(bit 5)= ON -> QL
// Si Input 4(bit 3) = ON i INPUT6(bit 5) = OFF -> VIA

// Per fer compatible amb més equips definim el equip amb un numero
// 0 = TIELINE VIA
// 1 = YAMAHA QL
#define PORT_A "VIA"
#define PORT_B "QL"

// Define PINS
// Botons i leds locals
#define POLSADOR_ROIG_PIN 16
#define POLSADOR_VERD_PIN 5
#define LED_ROIG_PIN 17
#define LED_VERD_PIN 18
#define MATRIX_PIN 4
#define DEBOUNCE_DELAY 100 // Delay debouncer

// Define Quantitat de leds
#define LED_COUNT 72 // 8x8 + 8

// Define sensor battery
#define BATTERY_PIN 36

// Definim PINS

// Amb la redifinicio dels PINS hem d'utilitzar una placa
// PCF8575 que la conectarem via I2C
// Els pins SCL 22 i SDA 23
// Referencia: https://www.prometec.net/mas-entradas-y-salidas-disponibles-esp32/
// Treballarem amb dos moduls, d'aquesta manera gestionem 16+16 = 32 GPIO extres
// Definim un GPEXTA i un GPEXTB (cal canviar la direcció de del GPEXTB soldant A0 a VDD)
PCF8575 GPEXTA(0x20);
PCF8575 GPEXTB(0x21);

/* AQUESTS PINS NO S'UTILITZEN

// ******** EQUIP A
// VIA
// I Input O Output
// Només son 4 i no hi ha tensió
// TODO: FAREM SERVIR PLACA i2C PER AMPLIAR GPIO - CAL REDEFINIR
// const uint8_t GPIA_PIN[4] = {2, 15, 19, 0}; // El bit 1 encen el led local del ESp32 - Atenció 0 Pullup!!!
// const uint8_t GPOA_PIN[1] = {23};           // No tenim tants bits - Un sol bit de confirmacio
// El bit de confirmació pot obrir canal de la taula

// ******** EQUIP B
// YAMAHA QL
// I Input O Output
// 5 Ins 5 Outs
// TODO: FAREM SERVIR PLACA i2C PER AMPLIAR GPIO - CAL REDEFINIR
// uint8_t const GPIB_PIN[6] = {39, 34, 35, 32, 33, 25};
// uint8_t const GPOB_PIN[5] = {26, 27, 14, 12, 13};
// uint8_t const GPOB_PIN[5] = {26, 27, 17, 16, 13}; // El 12 donava problemes al fer boot, el 14 treu PWM
*/

// Declarem neopixels
Adafruit_NeoPixel llum(LED_COUNT, MATRIX_PIN, NEO_GRB + NEO_KHZ800);

// Definim els colors GRB
const uint8_t COLOR[][6] = {{0, 0, 0},        // 0- NEGRE
                            {0, 255, 0},      // 1- ROIG
                            {0, 0, 255},      // 2- BLAU
                            {0, 255, 255},    // 3- CEL
                            {255, 0, 0},      // 4- VERD
                            {128, 255, 0},    // 5- GROC
                            {128, 128, 0},    // 6- TARONJA
                            {255, 255, 255}}; // 7- BLANC

uint8_t funcio_local_num = 0;       // 0 = TALLY, 1 = CONDUCTOR, 2 = PRODUCTOR
uint8_t color_matrix[] = {0, 0, 0}; // 0 = TALLY, 1 = CONDUCTOR, 2 = PRODUCTOR

// Declarem el display LCD
LiquidCrystal_I2C lcd(0x27, 16, 2); // 0x27 adreça I2C 16 = Caracters 2= Linees

// Variables
bool debouncing_roig = false; // Flag debouncing
bool debouncing_verd = false; // Flag debouncing
unsigned long last_time_roig; // Temps debouncing roig
unsigned long last_time_verd; // Temps debouncing verd

// Fem arrays de dos valors la 0 és anterior la 1 actual
bool POLSADOR_LOCAL_ROIG[] = {false, false};
bool POLSADOR_LOCAL_VERD[] = {false, false};

// Valor dels leds (dels polsadors)
bool LED_LOCAL_ROIG = false;
bool LED_LOCAL_VERD = false;
bool led_roig[] = {false, false, false}; // 0 = Tally, 1 = COND, 2 = PROD
bool led_verd[] = {false, false, false}; // 0 = Tally, 1 = COND, 2 = PROD
uint16_t BATTERY_LOCAL_READ[] = {0, 0};

// Variables GPIO
// Fem arrays de dos valors. El primer valor 0 és anterior la 1 actual
// El segon valor es el PIN 0 = 1, 1=2..
// Els GPIx tenen dos valors per veure si han canviat.
bool GPIA[2][8] = {{false, false, false, false, false, false, false, false},
                   {false, false, false, false, false, false, false, false}}; // GPI que venen del equip A INPUTS
bool GPIB[2][8] = {{false, false, false, false, false, false, false, false},
                   {false, false, false, false, false, false, false, false}}; // GPI que venen del equip B INPUTS
bool GPOA[8] = {false, false, false, false, false, false, false, false};      // GPO que van al equip A OUTPUTS
bool GPOB[8] = {false, false, false, false, false, false, false, false};      // GPO que van al equip B OUTPUTS

// Variables de gestió
bool GPIA_CHANGE = false;  // Per saber si hi han canvis en el GPIA
bool GPIB_CHANGE = false;  // Per saber si hi han canvis en el GPIB
bool LOCAL_CHANGE = false; // Per saber si alguna cosa local ha canviat

unsigned long temps_set_config = 0;      // Temps que ha d'estar apretat per configuracio
const unsigned long temps_config = 3000; // Temps per disparar opció config
unsigned long temps_set_config_post = 0; // Temps per sortir config
bool pre_mode_configuracio = false;      // Inici mode configuració
bool mode_configuracio = false;          // Mode configuració
bool post_mode_configuracio = false;     // Final configuració

// Text linea 2 que envia MASTER
uint8_t display_text_1[] = {0, 0, 0}; // Primera linea 0 = TALLY, 1 = CONDUCTOR, 2 = PRODUCTOR
uint8_t display_text_2[] = {0, 0, 0}; // Segona linea 0 = TALLY, 1 = CONDUCTOR, 2 = PRODUCTOR

//     TEXT_1[] = "12345678""90123456"
//                          "HH:MM:SS"
//                          "NO CLOCK"
String TEXT_1[] = {"       ",  // 0
                   " TALLY ",  // 1
                   " COND  ",  // 2
                   " PROD  ",  // 3
                   "CONFIG:",  // 4
                   "NO LINK"}; // 5

//     TEXT_2[] = "1234567890123456"
String TEXT_2[] = {"                ",  // 0
                   " FORA DE SERVEI ",  // 1
                   " ERROR GPI VIA  ",  // 2
                   "  ERROR GPI QL  ",  // 3
                   "**** ON AIR ****",  // 4
                   "ORD PROD A COND ",  // 5
                   "ORD DE PRODUCTOR",  // 6
                   "ORD A CONDUCTOR ",  // 7
                   "ORD COND A PROD ",  // 8
                   "ORD DE CONDUCTOR",  // 9
                   "ORD A PRODUCTOR ",  // 10
                   "ORD PROD A ESTUD",  // 11
                   "ORDRES A ESTUDI ",  // 12
                   "ORD COND A ESTUD",  // 13
                   "TANCAT LOCALMENT",  // 14
                   "TANCAT DE ESTUDI",  // 15
                   "  MICRO TANCAT  ",  // 16
                   "* ON AIR LOCAL *",  // 17
                   "<MODE TALLY    >",  // 18
                   "<MODE CONDUCTOR>",  // 19
                   "<MODE PRODUCTOR>"}; // 20

// Replace with your network credentials (STATION)
const char *ssid = "exteriors";
const char *password = "exteriors#";
// TODO: Poder seleccionar via WEB la Wifi on connectar.

esp_now_peer_info_t slave;
int chan;

enum MessageType
{
  PAIRING,
  DATA,
  TALLY,
  BATERIA,
  CLOCK
};
MessageType messageType;

// Definim les funcions del Tally
enum TipusFuncio
{
  LLUM,
  CONDUCTOR,
  PRODUCTOR
};
TipusFuncio funcio_local = LLUM;

int counter = 0;

// Structure example to receive data
// Must match the sender structure
// TODO ELIMINAR
typedef struct struct_message
{
  uint8_t msgType;
  uint8_t id;
  float temp;
  float hum;
  unsigned int readingId;
} struct_message;
// ELIMINAR FINS AQUI

// Estructura pairing
typedef struct struct_pairing
{ // new structure for pairing
  uint8_t msgType;
  uint8_t id;
  uint8_t macAddr[6];
  uint8_t channel;
} struct_pairing;

// Estrucrtura dades per enviar a slaves
typedef struct struct_message_to_slave
{
  uint8_t msgType;
  // Borrar - No cal funcio uint8_t funcio;      // Identificador de la funcio del tally
  bool led_roig[3];       // Array LLUM, COND, PROD llum confirmació cond polsador vermell
  bool led_verd[3];       // Array LLUM, COND, PROD llum confirmació cond polsador verd
  uint8_t color_tally[3]; // Array LLUM, COND, PROD Color indexat del tally
  uint8_t text_2[3];      // Array LLUM, COND, PROD text per mostrar a pantalla
} struct_message_to_slave;

// Estrucrtura dades rebuda de slaves
typedef struct struct_message_from_slave
{
  uint8_t msgType;
  uint8_t id;     // Identificador del tally
  uint8_t funcio; // Identificador de la funcio del tally
  bool polsador_roig;
  bool polsador_verd;
} struct_message_from_slave;

// Estructura dades per rebre bateries
typedef struct struct_bateria_info
{
  uint8_t msgType;
  uint8_t id;             // Identificador del tally
  float volts;            // Lectura en volts
  float percent;          // Percentatge carrega
  unsigned int readingId; // Identificador de lectura
} struct__bateria_info;

// Estructura dades per rebre clock
// TODO

struct_message incomingReadings;  // Demo original per a eliminar ***********
struct_message outgoingSetpoints; // Demo original per a eliminar ************
struct_pairing pairingData;
struct_message_from_slave fromSlave; // dades del master cap al tally
struct_message_to_slave toSlave;     // dades del tally cap al master

AsyncWebServer server(80);
AsyncEventSource events("/events");

// TODO: Adaptar web als valors dels Tallys
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>TALLY SISTEM</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    p {  font-size: 1.2rem;}
    body {  margin: 0;}
    .topnav { overflow: hidden; background-color: #2f4468; color: white; font-size: 1.7rem; }
    .content { padding: 20px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
    .reading { font-size: 2.8rem; }
    .packet { color: #bebebe; }
    .card.temperature { color: #fd7e14; }
    .card.humidity { color: #1b78e2; }
    .card.operativa { color: #fd7e14; }
    .card.estat { color: #1b78e2; }
  </style>
</head>
<body>
  <div class="topnav">
    <h3>TALLY MASTER</h3>
  </div>
  <div class="content">
    <div class="cards">
      <div class="card operativa">
        <h4> TALLY 1 - OPERACIO</h4><p><span class="reading"><span id="f1"></span><span id="br1"></span><span id="bv1"></span></p><p class="packet">Reading ID: <span id="rt1"></span></p>
      </div>
      <div class="card estat">
        <h4> TALLY 1 - BATERIA</h4><p><span class="reading"><span id="h1"></span> &percnt;</span></p><p class="packet">Reading ID: <span id="rh1"></span></p>
      </div>
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> TALLY 1 </h4><p><span class="reading"><span id="t1"></span> &deg;C</span></p><p class="packet">Reading ID: <span id="rt1"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> BOARD #1 - HUMIDITY</h4><p><span class="reading"><span id="h1"></span> &percnt;</span></p><p class="packet">Reading ID: <span id="rh1"></span></p>
      </div>
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> TALLY 2</h4><p><span class="reading"><span id="t2"></span> &deg;C</span></p><p class="packet">Reading ID: <span id="rt2"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> BOARD #2 - HUMIDITY</h4><p><span class="reading"><span id="h2"></span> &percnt;</span></p><p class="packet">Reading ID: <span id="rh2"></span></p>
      </div>
    </div>
  </div>
<script>
if (!!window.EventSource) {
 var source = new EventSource('/events');
 
 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);
 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);
 
 source.addEventListener('message', function(e) {
  console.log("message", e.data);
 }, false);
 
 source.addEventListener('new_readings', function(e) {
  console.log("new_readings", e.data);
  var obj = JSON.parse(e.data);
  document.getElementById("t"+obj.id).innerHTML = obj.temperature.toFixed(2);
  document.getElementById("h"+obj.id).innerHTML = obj.humidity.toFixed(2);
  document.getElementById("rt"+obj.id).innerHTML = obj.readingId;
  document.getElementById("rh"+obj.id).innerHTML = obj.readingId;
 }, false);

 source.addEventListener('new_operativa', function(e) {
  console.log("new_operativa", e.data);
  var obj = JSON.parse(e.data);
  document.getElementById("f"+obj.id).innerHTML = obj.funcio.toFixed(2);
  document.getElementById("br"+obj.id).innerHTML = obj.boto_roig.toFixed(2);
  document.getElementById("bv"+obj.id).innerHTML = obj.boto_verd.toFixed(2);
 }, false);
}
</script>
</body>
</html>)rawliteral";

/* Notes
obj.temperature.toFixed(2); El dos del parentesis es per saber quants matrius arriben.
Per imprimir la variable posem el nom de la variable seguit del numero de registre "id"
*/

void readDataToSend()
{
  outgoingSetpoints.msgType = DATA;
  outgoingSetpoints.id = 0; // Servidor te la id 0, els esclaus la 1,2,3..
  outgoingSetpoints.temp = random(0, 40);
  outgoingSetpoints.hum = random(0, 100);
  outgoingSetpoints.readingId = counter++; // Cada vegada que enviem dades incrementem el contador
}

void comunicar_slaves()
{
  toSlave.msgType = TALLY;
  for (int i = 0; i < 3; i++)
  {
    toSlave.led_roig[i] = led_roig[i]; // Li pasem array complert
    toSlave.led_verd[i] = led_verd[i]; // Idem 3 valors LLUM, COND i PROD
    toSlave.color_tally[i] = color_matrix[i];
    toSlave.text_2[i] = display_text_2[i];
  }
  // Send message via ESP-NOW
  esp_err_t result = esp_now_send(NULL, (uint8_t *)&toSlave, sizeof(toSlave));
  if (result == ESP_OK)
  {
    Serial.println("Sent to slaves with success");
  }
  else
  {
    Serial.println("Error sending to slave data");
  }
}

// Simulem lectura de bateria
// TODO Unificar lectura volts i convertir a nivells
float readBateriaVolts()
{
  float volt = random(0, 40); // = analogRead(BATTERY_PIN)
  return volt;
}

// Simulem lectura percentatge bateria
float readBateriaPercent()
{
  uint8_t percent = random(0, 100);
  return percent;
}

void logica_GPO()
{
  // Aquesta funció s'encarrega de vigilar que no s'enviin combinacions de GPO
  // contradictories i crein problemes.
  // Cal desenvolupar una logica per cada cas.

  if (PORT_B == "QL")
  {

    /*
    Funcio dels GPO:
    Estem limitats als GPIO IN de la QL1 (5 max).
     Bit 0 - MUTE CONDUCTOR
     Bit 1 - COND Ordres Productor (vermell)
     Bit 2 - COND Ordres Estudi (verd)
     Bit 3 - PROD Ordres Conductors (vermell)
     Bit 4 - PROD Ordres Estudi (verd)

     Que han de fer les escenes de la QL:

     Quan rebem un Bit 1 - Fer MUTE del conductor a programa.

     Quan reben un Bit 2 - CONDUCTOR Ordres a Productor (vermell):
     Va associat a un mute del Bit1. (Muteja el micro del conductor a PGM, PA..)
     Unmute del canal CONDUCTOR que va al BUS del PRODUCTOR.
     Enviar un bit pel GPIO2 (confirmacio)

     Quan reben un Bit 3 - CONDUCTOR Ordres a Estudi (verd):
     Va associat a un mute del Bit1. (Muteja el micro del conductor a PGM, PA..)
     Fa unmute del canal del CONDUCTOR que va al BUS de ORDRES ESTUDI OUT.
     Envia un bit pel GPIO3 (confirmació)
     Fa unmute del canal ORDRES ESTUDI IN que va al BUS del CONDUCTOR.
     Podria fer una atenuació del PGM del BUS del conductor.


     Quan reben un Bit 4 - PRODUCTOR Ordres a Conductor (vermell):
     Desmutejar micro productor del BUS que va als auriculars del conductor i del productor,
     (opcionalment atenuar PGM dels auriculars del conductor i del productor - pq aquest s'escolti a si mateix),
     enviar un bit de confirmació per la sortida 4.

     Quan reben un Bit 5 - PRODUCTOR Ordres Estudi (verd):
     Desmutejar micro productor del BUS que va a les ordres del estudi i als auriculars del productor,
     (opcionalment atenuar PGM dels auriculars del productor - pq aquest s'escolti a si mateix),
     enviar un bit de confirmació per la sortida 5.
       */

    // Gestió del Mute del conductor GPIO1 en funció dels bits.

    if (GPOB[1])
    // Ordres CONDUCTOR A PRODUCTOR
    // COND dona Ordres a Productor (polsador vermell Conductor)
    {
      GPOB[0] = true;  // Fem mute del conductor
      GPOB[2] = false; // Si tenim GPOB1 apaguem la resta
      GPOB[3] = false;
      GPOB[4] = false;
    }
    // Prioritzem les ordres del conductor al estudi

    if (!GPOB[1] && GPOB[2])
    // Ordres CONDUCTOR A ESTUDI
    // Si COND dona Ordres a Estudi i no a productor
    {
      GPOB[0] = true;  // Fem mute del conductor
      GPOB[3] = false; // Si tenim GPOB2 i no GPOB1 apaguem la resta
      GPOB[4] = false;
    }
    // Prioritzem les ordres del productor al conductor

    if (!GPOB[1] && !GPOB[2] && GPOB[3])
    // Ordres PRODUCTOR A CONDUCTOR
    // Si PROD dona ordres a CONDUCTOR i CONDUCTOR no dona ordres ni a Estudi ni Productor
    {
      GPOB[0] = false; // No fem mute del Conductor
      GPOB[4] = false;
    }

    if (!GPOB[1] && !GPOB[2] && !GPOB[3] && GPOB[4])
    // Si PROD dona ordres a ESTUDI i no dona a CONDUCTOR ni COND dona ordres a ESTUDI ni PRODUCTOR
    {
      GPOB[0] = false; // No fem mute del Conductor
    }

    // ATENCIO QUE PASA SI FAIG UN MUTE DEL CONDUCTOR DES DE TAULA???
    if (!GPOB[0] && !GPOB[1] && !GPOB[2] && !GPOB[3] && !GPOB[4])
    // Si no apretem cap botó fem i no tenim UNMUTE del CONDUCTOR treiem el MUTE COND
    {
      GPOB[0] = false; // No fem mute del Conductor
      // Si no apretem cap botó ens assegurem unmute del conductor
    }
  }
}

void escriure_GPO()
{
  // Que passa si apretem tots els polsadors alhora. La QL intententara carregar 4
  // memories al mateix temps -> Cosa que la pot liar parda.
  // La idea es mirar que tan sols un GPOB escrigui els valors.
  // Per no complicar el codi ho farem amb un void.

  logica_GPO(); // Filtre per enviar només un GPO i fer logica Mute COND

  
  for (uint8_t i = 8; i < 16; i++)
  {
    GPEXTA.digitalWrite(i, GPOA[i]);
    GPEXTB.digitalWrite(i, GPOB[i]);
    /*if (debug)
    {
      Serial.print("GPOA PIN: ");
      Serial.print(i);
      Serial.print("Bit: ");
      Serial.println(GPOA[i]);

      Serial.print("GPOB PIN: ");
      Serial.print(i);
      Serial.print("Bit: ");
      Serial.println(GPOB[i]);
    }
    */
  }
}

void escriure_display_1(uint8_t txt1)
{
  lcd.setCursor(0, 0); // Situem cursor primer caracter, primera linea
  lcd.print(TEXT_1[txt1]);
}

void escriure_display_2(uint8_t txt2)
{
  lcd.setCursor(0, 1); // Primer caracter, segona linea
  lcd.print(TEXT_2[txt2]);
}

void escriure_display_clock(uint8_t temps_clock)
{
  lcd.setCursor(9, 0);   // Caracter 9, primera linea
  lcd.print("HH:MM:SS"); // TODO -> POSAR TEMPS
}
// Posar llum a un color
void escriure_matrix(uint8_t color)
{
  // GBR
  uint8_t G = COLOR[color][1];
  uint8_t B = COLOR[color][2];
  uint8_t R = COLOR[color][0];
  for (int i = 0; i < LED_COUNT; i++)
  {
    llum.setPixelColor(i, llum.Color(G, B, R));
  }
  llum.show();
  if (debug)
  {
    Serial.print("Color: ");
    Serial.println(color);
    Serial.print("R: ");
    Serial.println(R);
    Serial.print("G: ");
    Serial.println(G);
    Serial.print("B: ");
    Serial.println(B);
  }
}

void logica_polsadors_locals()
{
  if (!mode_configuracio && funcio_local_num == 1) // Si no estic en configuracio i SOC CONDUCTOR
  {
    if (!(POLSADOR_LOCAL_ROIG[0] && POLSADOR_LOCAL_VERD[0]))
    // Si no apreto els dos polsadors simultaneament
    {
      // El MUTE del micro CONDUCTOR es fa en rebre confirmació
      GPOB[1] = POLSADOR_LOCAL_ROIG[0]; // Si funcio=conductor enviem bit a gpo1 vermell
      GPOB[2] = POLSADOR_LOCAL_VERD[0]; // si funcio=conductor enviem bit a gpo2 verd
      LOCAL_CHANGE = false;
      escriure_GPO();
      if (POLSADOR_LOCAL_ROIG[0])
      {
        if (debug)
        {
          Serial.print("ORDRES COND A PRODUCTOR");
        }
      }
      if (POLSADOR_LOCAL_VERD[0])
      {
        if (debug)
        {
          Serial.print("ORDRES COND A ESTUDI");
        }
      }
      //************* CAL ELIMINAR AIXO *********
      // Simulem GPO indicant GPI (simulem la QL)
      GPIB[1][1] = GPOB[1];
      GPIB[1][2] = GPOB[2];
      GPIB_CHANGE = true;
      //************* CAL ELIMINAR AIXO *********
    }
  }

  if (!mode_configuracio && funcio_local_num == 2) // SOC PRODUCTOR
  {
    if (!(POLSADOR_LOCAL_ROIG[0] && POLSADOR_LOCAL_VERD[0]))
    // Si no apreto els dos polsadors simultaneament
    {
      GPOB[3] = POLSADOR_LOCAL_ROIG[0]; // Si funcio=productor enviem el bit a gpo4 roig
      GPOB[4] = POLSADOR_LOCAL_VERD[0]; // Si funcio=productor enviem el bit a gpo5 verd
      LOCAL_CHANGE = false;
      escriure_GPO();

      if (POLSADOR_LOCAL_ROIG[0])
      {
        if (debug)
        {
          Serial.print("ORDRES PROD AL CONDUCTOR");
        }
      }
      if (POLSADOR_LOCAL_VERD[0])
      {
        if (debug)
        {
          Serial.print("ORDRES PROD A ESTUDI");
        }
      }

      if (debug)
      {
        Serial.print("UN SOL POLSADOR PRODUCTOR");
      }
      //************* CAL ELIMINAR AIXO *********
      // Simulem GPO indicant GPI (simulem la QL)
      GPIB[1][3] = GPOB[3];
      GPIB[1][4] = GPOB[4];
      GPIB_CHANGE = true;
      //************* CAL ELIMINAR AIXO *********
    }
  }
}

void llegir_polsadors()
{
  POLSADOR_LOCAL_ROIG[1] = !digitalRead(POLSADOR_ROIG_PIN); // Els POLSADOR son PULLUP per tant els llegirem al revés
  POLSADOR_LOCAL_VERD[1] = !digitalRead(POLSADOR_VERD_PIN);
  // Detecció canvi de POLSADOR locals
  if ((POLSADOR_LOCAL_ROIG[0] != POLSADOR_LOCAL_ROIG[1]) && (!debouncing_roig))
  {
    /// HEM POLSAT EL POLSADOR ROIG PERO NO VALIDEM
    last_time_roig = millis();
    debouncing_roig = true;
  }

  if ((POLSADOR_LOCAL_ROIG[0] != POLSADOR_LOCAL_ROIG[1]) && (debouncing_roig))
  {
    if ((millis() - last_time_roig) > DEBOUNCE_DELAY)
    {
      // HA PASSAT EL TEMPS DE DEBOUNCING
      LOCAL_CHANGE = true;
      POLSADOR_LOCAL_ROIG[0] = POLSADOR_LOCAL_ROIG[1];
      debouncing_roig = false;
      if (debug)
      {
        Serial.print("POLSADOR local ROIG: ");
        Serial.println(POLSADOR_LOCAL_ROIG[0]);
      }
    }
  }

  if ((POLSADOR_LOCAL_VERD[0] != POLSADOR_LOCAL_VERD[1]) && (!debouncing_verd))
  {
    /// HEM POLSAT EL POLSADOR VERD PERO NO VALIDEM
    last_time_verd = millis();
    debouncing_verd = true;
  }
  if ((POLSADOR_LOCAL_VERD[0] != POLSADOR_LOCAL_VERD[1]) && (debouncing_verd))
  {

    if ((millis() - last_time_verd) > DEBOUNCE_DELAY)
    {
      LOCAL_CHANGE = true;
      POLSADOR_LOCAL_VERD[0] = POLSADOR_LOCAL_VERD[1];
      debouncing_verd = false;
      if (debug)
      {
        Serial.print("POLSADOR local VERD: ");
        Serial.println(POLSADOR_LOCAL_VERD[0]);
      }
    }
  }
}

void escriure_leds()
{
  digitalWrite(LED_ROIG_PIN, LED_LOCAL_ROIG);
  digitalWrite(LED_VERD_PIN, LED_LOCAL_VERD);
}

void logica_gpi()
{
  if (debug)
  {
    Serial.println("GPI CHANGE");
  }
  if ((PORT_A == "VIA") && (PORT_B == "QL"))
  /*
    VIA:
    GPO 0 -> LLUM
    GPO 3 -> CONECTAT

    QL:
    GPO 0: MIC COND ON
    GPO 1: Confirmació COND Ordres Productor (vermell)
    GPO 2: Confirmació COND Ordres Estudi (verd)
    GPO 3: Confirmació PROD Ordres Conductors (vermell)
    GPO 4: Confirmació PROD Ordres Estudi (verd
    GPO 5: Presencia QL (tensió)
  */

  { // TENIM VIA I QL CONNECTATS
    if (GPIA[0][3] && GPIB[0][5])
    {
      if ((GPIB[0][1] && (GPIB[0][2] || GPIB[0][3] || GPIB[0][4])) ||
          (GPIB[0][2] && (GPIB[0][1] || GPIB[0][3] || GPIB[0][4])) ||
          (GPIB[0][3] && (GPIB[0][1] || GPIB[0][2] || GPIB[0][4])) ||
          (GPIB[0][4] && (GPIB[0][1] || GPIB[0][2] || GPIB[0][3])))
      // SI tenim més de una confirmació simultanea el sistema falla
      // DONAREM ERRROR I APAGUAREM TALLYS
      {
        display_text_2[0] = 3; //"  ERROR GPI QL  "
        display_text_2[1] = 3; //"  ERROR GPI QL  "
        display_text_2[2] = 3; //"  ERROR GPI QL  "
        color_matrix[0] = 0;   // Negre (LLUM)
        color_matrix[1] = 0;   // Negre (CONDUCTOR)
        color_matrix[2] = 0;   // Negre (PRODUCTOR)
        led_roig[1] = false;
        led_verd[1] = false;
        led_roig[2] = false;
        led_verd[2] = false;
        if (debug)
        {
          Serial.println("Falla QL A + B + ON AIR + MIC ON");
        }
      }
      else
      {
        // Presencia de VIA I QL ==================================================
        if (GPIA[0][0])
        {
          // Tenim BIT ON AIR *****************************************************

          if (GPIB[0][0])
          {
            // Mic COND Obert -- CAL MIRAR SI EL BIT ES OBERT o INDICA MUTE
            //  Girar la lógica si cal

            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si notenim cap confirmació d'ordres
              display_text_2[0] = 4; //"**** ON AIR ****"
              display_text_2[1] = 4; //"**** ON AIR ****"
              display_text_2[2] = 4; //"**** ON AIR ****"
              color_matrix[0] = 1;   // Vermell (LLUM)
              color_matrix[1] = 1;   // Vermell (CONDUCTOR)
              color_matrix[2] = 1;   // Vermell (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC ON");
              }
            }
            if (!GPIB[0][1] && !GPIB[0][2] && GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres PROD A CONDUCTOR
              display_text_2[0] = 5; //"ORD PROD A COND ", //5
              display_text_2[1] = 6; //"ORD DE PRODUCTOR", //6
              display_text_2[2] = 7; //"ORD A CONDUCTOR ", //7
              color_matrix[0] = 1;   // Vermell (LLUM)
              color_matrix[1] = 1;   // Vermell (CONDUCTOR)
              color_matrix[2] = 1;   // Blau (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = true;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC ON + ORDRES PROD 2 COND");
              }
            }
            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && GPIB[0][4])
            {
              // Si tenim confirmació ordres PROD A ESTUDI
              display_text_2[0] = 11; //"ORD PROD A ESTUD", //11
              display_text_2[1] = 4;  //"**** ON AIR ****", //4
              display_text_2[2] = 12; //"ORDRES A ESTUDI ", //12
              color_matrix[0] = 1;    // Vermell (LLUM)
              color_matrix[1] = 1;    // Vermell (CONDUCTOR)
              color_matrix[2] = 3;    // Cel (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = true;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC ON + ORDRES PROD 2 ESTU");
              }
            }
          }
          else
          // Si no tenim micro COND Obert
          {
            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si no tenim cap ordre Nomes micro tancat a QL
              display_text_2[0] = 14; //"TANCAT LOCALMENT", //14
              display_text_2[1] = 14; //"TANCAT LOCALMENT", //14
              display_text_2[2] = 14; //"TANCAT LOCALMENT", //14
              color_matrix[0] = 6;    // Taronja (LLUM)
              color_matrix[1] = 6;    // Taronja (CONDUCTOR)
              color_matrix[2] = 6;    // Taronja (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC OFF + NO ORDRES");
              }
            }
            if (GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres COND A PROD
              display_text_2[0] = 8;  //"ORD COND A PROD ", //8
              display_text_2[1] = 10; //"ORD A PRODUCTOR ", //10
              display_text_2[2] = 9;  //"ORD DE CONDUCTOR", //9
              color_matrix[0] = 1;    // Vermell (LLUM)
              color_matrix[1] = 2;    // Blau (CONDUCTOR)
              color_matrix[2] = 1;    // Vermell (PRODUCTOR)
              led_roig[1] = true;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC OFF + ORDRES COND 2 PROD");
              }
            }
            if (!GPIB[0][1] && GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres COND A ESTU
              display_text_2[0] = 13; //"ORD COND A ESTUD", //13
              display_text_2[1] = 12; //"ORDRES A ESTUDI ", //12
              display_text_2[2] = 4;  //"**** ON AIR ****", //4
              color_matrix[0] = 1;    // Vermell (LLUM)
              color_matrix[1] = 3;    // Cel (CONDUCTOR)
              color_matrix[2] = 1;    // Vermell (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = true;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + ON AIR + MIC OFF + ORDRES COND 2 ESTU");
              }
            }
          }
        }
        else
        {
          // No tenim BIT ON AIR **********************************************
          if (GPIB[0][0])
          {
            // Mic COND Obert -- CAL MIRAR SI EL BIT ES OBERT o INDICA MUTE
            //  Girar la lógica si cal
            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si notenim cap confirmació d'ordres
              display_text_2[0] = 15; //"TANCAT DE ESTUDI", //15
              display_text_2[1] = 15; //"TANCAT DE ESTUDI", //15
              display_text_2[2] = 15; //"TANCAT DE ESTUDI", //15
              color_matrix[0] = 5;    // Groc (LLUM)
              color_matrix[1] = 5;    // Groc (CONDUCTOR)
              color_matrix[2] = 5;    // Groc (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC ON");
              }
            }
            if (!GPIB[0][1] && !GPIB[0][2] && GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres PROD A CONDUCTOR
              display_text_2[0] = 5; //"ORD PROD A COND ", //5
              display_text_2[1] = 6; //"ORD DE PRODUCTOR", //6
              display_text_2[2] = 7; //"ORD A CONDUCTOR ", //7
              color_matrix[0] = 5;   // Groc (LLUM)
              color_matrix[1] = 5;   // Groc (CONDUCTOR)
              color_matrix[2] = 1;   // Blau (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = true;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC ON + ORDRES PROD 2 COND");
              }
            }
            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && GPIB[0][4])
            {
              // Si tenim confirmació ordres PROD A ESTUDI
              display_text_2[0] = 11; //"ORD PROD A ESTUD", //11
              display_text_2[1] = 4;  //"**** ON AIR ****", //4
              display_text_2[2] = 12; //"ORDRES A ESTUDI ", //12
              color_matrix[0] = 3;    // Groc (LLUM)
              color_matrix[1] = 3;    // Groc (CONDUCTOR)
              color_matrix[2] = 3;    // Cel (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = true;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC ON + ORDRES PROD 2 ESTU");
              }
            }
          }
          else
          // Si no tenim micro COND Obert
          {
            if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si no tenim cap ordre Nomes micro tancat a QL
              display_text_2[0] = 16; //"  MICRO TANCAT  "; //16
              display_text_2[1] = 16; //"  MICRO TANCAT  "; //16
              display_text_2[2] = 16; //"  MICRO TANCAT  "; //16
              color_matrix[0] = 7;    // Blanc (LLUM)
              color_matrix[1] = 7;    // Blanc (CONDUCTOR)
              color_matrix[2] = 7;    // Blanc (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC OFF + NO ORDRES");
              }
            }
            if (GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres COND A PROD
              display_text_2[0] = 8;  //"ORD COND A PROD ", //8
              display_text_2[1] = 10; //"ORD A PRODUCTOR ", //10
              display_text_2[2] = 9;  //"ORD DE CONDUCTOR", //9
              color_matrix[0] = 7;    // Blanc (LLUM)
              color_matrix[1] = 2;    // Blau (CONDUCTOR)
              color_matrix[2] = 7;    // Blanc (PRODUCTOR)
              led_roig[1] = true;
              led_verd[1] = false;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC OFF + ORDRES COND 2 PROD");
              }
            }
            if (!GPIB[0][1] && GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
            {
              // Si tenim confirmació ordres COND A ESTU
              display_text_2[0] = 13; //"ORD COND A ESTUD", //13
              display_text_2[1] = 12; //"ORDRES A ESTUDI ", //12
              display_text_2[2] = 16; //"  MICRO TANCAT  "; //16
              color_matrix[0] = 7;    // Blanc (LLUM)
              color_matrix[1] = 3;    // Cel (CONDUCTOR)
              color_matrix[2] = 7;    // Blanc (PRODUCTOR)
              led_roig[1] = false;
              led_verd[1] = true;
              led_roig[2] = false;
              led_verd[2] = false;
              if (debug)
              {
                Serial.println(" A + B + OFF AIR + MIC OFF + ORDRES COND 2 ESTU");
              }
            }
          }
        }
      }
    }

    if (!GPIA[0][3] && GPIB[0][5])
    {
      if ((GPIB[0][1] && (GPIB[0][2] || GPIB[0][3] || GPIB[0][4])) ||
          (GPIB[0][2] && (GPIB[0][1] || GPIB[0][3] || GPIB[0][4])) ||
          (GPIB[0][3] && (GPIB[0][1] || GPIB[0][2] || GPIB[0][4])) ||
          (GPIB[0][4] && (GPIB[0][1] || GPIB[0][2] || GPIB[0][3])))
      // SI tenim més de una confirmació simultanea el sistema falla
      // DONAREM ERRROR I APAGUAREM TALLYS
      {
        display_text_2[0] = 3; //"  ERROR GPI QL  ", //3
        display_text_2[1] = 3; //"  ERROR GPI QL  ", //3
        display_text_2[2] = 3; //"  ERROR GPI QL  ", //3
        color_matrix[0] = 0;   // Negre (LLUM)
        color_matrix[1] = 0;   // Negre (CONDUCTOR)
        color_matrix[2] = 0;   // Negre (PRODUCTOR)
        led_roig[1] = false;
        led_verd[1] = false;
        led_roig[2] = false;
        led_verd[2] = false;
        if (debug)
        {
          Serial.println("Falla  A + B + ON AIR + MIC ON");
        }
      }
      else
      {

        // Presencia nomes de QL ==============================================
        if (GPIB[0][0])
        {
          // Mic COND Obert -- CAL MIRAR SI EL BIT ES OBERT o INDICA MUTE
          //  Girar la lógica si cal
          if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
          {
            // Si notenim cap confirmació d'ordres
            display_text_2[0] = 17; // "* ON AIR LOCAL *", //17
            display_text_2[1] = 17; // "* ON AIR LOCAL *", //17
            display_text_2[2] = 17; // "* ON AIR LOCAL *", //17
            color_matrix[0] = 4;    // Verd (LLUM)
            color_matrix[1] = 4;    // Verd (CONDUCTOR)
            color_matrix[2] = 4;    // Verd (PRODUCTOR)
            led_roig[1] = false;
            led_verd[1] = false;
            led_roig[2] = false;
            led_verd[2] = false;
            if (debug)
            {
              Serial.println(" NO A + B + MIC ON");
            }
          }
          if (!GPIB[0][1] && !GPIB[0][2] && GPIB[0][3] && !GPIB[0][4])
          {
            // Si tenim confirmació ordres PROD A CONDUCTOR
            display_text_2[0] = 5; //"ORD PROD A COND ", //5
            display_text_2[1] = 6; //"ORD DE PRODUCTOR", //6
            display_text_2[2] = 7; //"ORD A CONDUCTOR ", //7
            color_matrix[0] = 4;   // Verd (LLUM)
            color_matrix[1] = 4;   // Verd (CONDUCTOR)
            color_matrix[2] = 2;   // Blau (PRODUCTOR)
            led_roig[1] = false;
            led_verd[1] = false;
            led_roig[2] = true;
            led_verd[2] = false;
            if (debug)
            {
              Serial.println(" NO A + B + MIC ON + ORDRES PROD 2 COND");
            }
          }
          if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && GPIB[0][4])
          {
            // Si tenim confirmació ordres PROD A ESTUDI
            display_text_2[0] = 11; // "ORD PROD A ESTUD", //11
            display_text_2[1] = 17; // "* ON AIR LOCAL *", //17
            display_text_2[2] = 12; // "ORDRES A ESTUDI ", //12
            color_matrix[0] = 4;    // Verd (LLUM)
            color_matrix[1] = 4;    // Verd (CONDUCTOR)
            color_matrix[2] = 3;    // Cel (PRODUCTOR)
            led_roig[1] = false;
            led_verd[1] = false;
            led_roig[2] = false;
            led_verd[2] = true;
            if (debug)
            {
              Serial.println(" NO A + B + MIC ON + ORDRES PROD 2 ESTU");
            }
          }
        }
        else
        // Si no tenim micro COND Obert
        {
          if (!GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
          {
            // Si no tenim cap ordre Nomes micro tancat a QL
            display_text_2[0] = 14; //"TANCAT LOCALMENT", //14
            display_text_2[1] = 14; //"TANCAT LOCALMENT", //14
            display_text_2[2] = 14; //"TANCAT LOCALMENT", //14
            color_matrix[0] = 6;    // Taronja (LLUM)
            color_matrix[1] = 6;    // Taronja (CONDUCTOR)
            color_matrix[2] = 6;    // Taronja (PRODUCTOR)
            led_roig[1] = false;
            led_verd[1] = false;
            led_roig[2] = false;
            led_verd[2] = false;
            if (debug)
            {
              Serial.println(" NO A + B  + MIC OFF + NO ORDRES");
            }
          }
          if (GPIB[0][1] && !GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
          {
            // Si tenim confirmació ordres COND A PROD
            display_text_2[0] = 8;  // "ORD COND A PROD ", //8
            display_text_2[1] = 10; //"ORD A PRODUCTOR ", //10
            display_text_2[2] = 9;  // "ORD DE CONDUCTOR", //9
            color_matrix[0] = 4;    // Verd (LLUM)
            color_matrix[1] = 2;    // Blau (CONDUCTOR)
            color_matrix[2] = 4;    // Verd (PRODUCTOR)
            led_roig[1] = true;
            led_verd[1] = false;
            led_roig[2] = false;
            led_verd[2] = false;
            if (debug)
            {
              Serial.println(" NO A + B + MIC OFF + ORDRES COND 2 PROD");
            }
          }
          if (!GPIB[0][1] && GPIB[0][2] && !GPIB[0][3] && !GPIB[0][4])
          {
            // Si tenim confirmació ordres COND A ESTU
            display_text_2[0] = 13; //"ORD COND A ESTUD", //13
            display_text_2[1] = 12; //"ORDRES A ESTUDI ", //12
            display_text_2[2] = 17; //"* ON AIR LOCAL *", //17
            color_matrix[0] = 4;    // Verd (LLUM)
            color_matrix[1] = 3;    // Cel (CONDUCTOR)
            color_matrix[2] = 4;    // Verd (PRODUCTOR)
            led_roig[1] = false;
            led_verd[1] = true;
            led_roig[2] = false;
            led_verd[2] = false;
            if (debug)
            {
              Serial.println(" NO A + B + ON AIR + MIC OFF + ORDRES COND 2 ESTU");
            }
          }
        }
      }
    }

    if (GPIA[0][3] && !GPIB[0][5])
    {
      // Presencia de VIA PERO NO DE QL ========================================
      if (GPIA[0][0])
      {
        display_text_2[0] = 4; // "**** ON AIR ****", //4
        display_text_2[1] = 4; // "**** ON AIR ****", //4
        display_text_2[2] = 4; // "**** ON AIR ****", //4
        color_matrix[0] = 1;   // Vermell (LLUM)
        color_matrix[1] = 1;   // Vermell (CONDUCTOR)
        color_matrix[2] = 1;   // Vermell (PRODUCTOR)
        led_roig[1] = false;
        led_verd[1] = false;
        led_roig[2] = false;
        led_verd[2] = false;
        if (debug)
        {
          Serial.println(" A + NO B + ON AIR ");
        }
      }
      else
      {
        display_text_2[0] = 15; // "TANCAT DE ESTUDI", //15
        display_text_2[1] = 15; // "TANCAT DE ESTUDI", //15
        display_text_2[2] = 15; // "TANCAT DE ESTUDI", //15
        color_matrix[0] = 7;    // Blanc (LLUM)
        color_matrix[1] = 7;    // Blanc (CONDUCTOR)
        color_matrix[2] = 7;    // Blanc (PRODUCTOR)
        led_roig[1] = false;
        led_verd[1] = false;
        led_roig[2] = false;
        led_verd[2] = false;
        if (debug)
        {
          Serial.println(" A + NO B + OFF AIR");
        }
      }
    }

    if (!GPIA[0][3] && !GPIB[0][5])
    {
      display_text_2[0] = 1; // " FORA DE SERVEI ", //1
      display_text_2[1] = 1; // " FORA DE SERVEI ", //1
      display_text_2[2] = 1; // " FORA DE SERVEI ", //1
      color_matrix[0] = 0;   // NEGRE (LLUM)
      color_matrix[1] = 0;   // NEGRE (CONDUCTOR)
      color_matrix[2] = 0;   // NEGRE (PRODUCTOR)
      led_roig[1] = false;
      led_verd[1] = false;
      led_roig[2] = false;
      led_verd[2] = false;
      if (debug)
      {
        Serial.println(" NO A + NO B ");
      }
    }
  }
  GPIA_CHANGE = false;
  GPIB_CHANGE = false;
}

void llegir_gpi()
{
  // PORT A
  for (uint8_t i = 0; i < 8; i++) // Llegim els 8 GPI del PORT A i B
  {

    if (PORT_A == "VIA")
    {
      GPIA[1][i] = !GPEXTA.digitalRead(i); // GPIX[1] => Actual  GPIX[0] => Anterior
      if (GPIA[0][i] != GPIA[1][i])
      {
        // GPI CANVIAT
        GPIA_CHANGE = true;
        GPIA[0][i] = GPIA[1][i];
        if (debug)
        {
          Serial.print("POLSADOR GPIA");
          Serial.print(i);
          Serial.print(": ");
          Serial.println(GPIA[0][i]);
        }
      }
      else
      {
        GPIA_CHANGE = false;
      }
    }
  }
  // PORT B
  for (uint8_t i = 0; i < 8; i++) // Llegim els 8 GPI del PORT A i B
  {
    if (PORT_B == "QL") // QL va amb PULLUP Logica inversa
    {
      GPIB[1][i] = GPEXTB.digitalRead(i); // GPIX[1] => Actual  GPIX[0] => Anterior
      if (GPIB[0][i] != GPIB[1][i])
      {
        // GPI CANVIAT
        GPIB_CHANGE = true;
        GPIB[0][i] = GPIB[1][i];
        if (debug)
        {
          Serial.print("POLSADOR GPIB");
          Serial.print(i);
          Serial.print(": ");
          Serial.println(GPIB[0][i]);
        }
      }
      else
      {
        GPIB_CHANGE = false;
      }
    }
  }
  // PER PROVAR SENSE QL ni VIA CAL ELIMINAR ****************
  GPIA[0][3] = true; //SIMULEM VIA
  GPIB[0][5] = true; //SIMULEM QL
}

// ---------------------------- esp_ now -------------------------
void printMAC(const uint8_t *mac_addr)
{
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

bool addPeer(const uint8_t *peer_addr)
{ // add pairing Funció per afgir Peers
  memset(&slave, 0, sizeof(slave));
  const esp_now_peer_info_t *peer = &slave;
  memcpy(slave.peer_addr, peer_addr, 6);

  slave.channel = chan; // pick a channel
  slave.encrypt = 0;    // no encryption
  // check if the peer exists
  bool exists = esp_now_is_peer_exist(slave.peer_addr);
  if (exists)
  {
    // Slave already paired.
    Serial.println("Already Paired");
    return true;
  }
  else
  {
    esp_err_t addStatus = esp_now_add_peer(peer);
    if (addStatus == ESP_OK)
    {
      // Pair success
      Serial.println("Pair success");
      return true;
    }
    else
    {
      Serial.println("Pair failed");
      return false;
    }
  }
}

// callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success to " : "Delivery Fail to ");
  printMAC(mac_addr);
  Serial.println();
}

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  Serial.print(len);
  Serial.print(" bytes of data received from : ");
  printMAC(mac_addr);
  Serial.println();
  StaticJsonDocument<1000> root;
  String payload;
  uint8_t type = incomingData[0]; // first message byte is the type of message
  if (!mode_configuracio)         // Si no estem en mode configuració
  {
    switch (type)
    {
    case DATA: // the message is data type
      memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
      // create a JSON document with received data and send it by event to the web page
      root["id"] = incomingReadings.id;
      root["temperature"] = incomingReadings.temp;
      root["humidity"] = incomingReadings.hum;
      root["readingId"] = String(incomingReadings.readingId);
      serializeJson(root, payload);
      Serial.print("event send :");
      serializeJson(root, Serial);
      events.send(payload.c_str(), "new_readings", millis());
      Serial.println();
      break;

    case TALLY: // Missatge del SLAVE TALLY
      memcpy(&fromSlave, incomingData, sizeof(fromSlave));
      // create a JSON document with received data and send it by event to the web page
      root["id"] = fromSlave.id;
      root["funcio"] = fromSlave.funcio;
      root["boto_roig"] = fromSlave.polsador_roig;
      root["boto_verd"] = fromSlave.polsador_verd;
      serializeJson(root, payload);
      Serial.print("event send :");
      serializeJson(root, Serial);
      events.send(payload.c_str(), "new_operativa", millis());
      Serial.println();
      if (debug)
      {
        Serial.print("Tally ID = ");
        Serial.println(fromSlave.id);
        Serial.print("Funció  = ");
        Serial.println(fromSlave.funcio);
        Serial.print("Led roig = ");
        Serial.println(fromSlave.polsador_roig);
        Serial.print("Led verd= ");
        Serial.println(fromSlave.polsador_verd);
      }
      // PROCESSAR DADES REBUDES
      break;

    case BATERIA: // Missatge del SLAVE TALLY
      memcpy(&fromSlave, incomingData, sizeof(fromSlave));
      if (debug)
      {
        /*
        Imprimir valors també a web
        Serial.print("Tally ID = ");
        Serial.println(fromSlave.id)
            Serial.print("Funció  = ");
        Serial.println(fromSlave.funcio);
        Serial.print("Led roig = ");
        Serial.println(fromSlave.polsador_roig);
        Serial.print("Led verd= ");
        Serial.println(fromSlave.polsador_verd);
        */
      }
      // PROCESSAR DADES REBUDES
      break;

    case PAIRING: // the message is a pairing request
      memcpy(&pairingData, incomingData, sizeof(pairingData));
      Serial.println(pairingData.msgType);
      Serial.println(pairingData.id);
      Serial.print("Pairing request from: ");
      printMAC(mac_addr);
      Serial.println();
      Serial.println(pairingData.channel);
      if (pairingData.id > 0)
      { // do not replay to server itself (No es respon a ell mateix)
        if (pairingData.msgType == PAIRING)
        {
          pairingData.id = 0; // 0 is server
          // Server is in AP_STA mode: peers need to send data to server soft AP MAC address
          WiFi.softAPmacAddress(pairingData.macAddr);
          pairingData.channel = chan;
          Serial.println("send response");
          esp_err_t result = esp_now_send(mac_addr, (uint8_t *)&pairingData, sizeof(pairingData)); // Respon al emissor
          addPeer(mac_addr);
        }
      }
      break;
    }
  }
}

void Menu_configuracio()
{
  // Desenvolupar aqui el mode configuració
  // TODO:  Seleccionar entre LLUM, CONDUCTOR i PRODUCTOR
  // Veure com generem menu
  // local_text_1 = 4; //CONFIG
  // local_text_2 = 18; //MODE TALLY
  int select[] = {18, 19, 20};
  int sel = 0;
  escriure_display_1(4);
  post_mode_configuracio = false;
  while (mode_configuracio)
  {
    escriure_display_2(select[sel]); // Dibuixem la opcio
    /*
    if (debug)
    {
      Serial.print("Sel: ");
      Serial.println(sel);
    }
    */
    LOCAL_CHANGE = false;
    llegir_polsadors(); // Llegim els polsadors
    if (LOCAL_CHANGE)
    {
      if (POLSADOR_LOCAL_ROIG[0] && !POLSADOR_LOCAL_VERD[0] && !post_mode_configuracio)
      {
        if (sel == 0)
        {
          sel = 2;
        }
        else
        {
          sel = (sel - 1);
        }
        if (debug)
        {
          Serial.print("Sel: ");
          Serial.println(sel);
        }
      }
      if (POLSADOR_LOCAL_VERD[0] && !POLSADOR_LOCAL_ROIG[0] && !post_mode_configuracio)
      {
        if (sel == 2)
        {
          sel = 0;
        }
        else
        {
          sel = (sel + 1);
        }
        if (debug)
        {
          Serial.print("Sel: ");
          Serial.println(sel);
        }
      }
      if (POLSADOR_LOCAL_VERD[0] && !POLSADOR_LOCAL_ROIG[0] && post_mode_configuracio)
      {
        post_mode_configuracio = false;
      }
      if (!POLSADOR_LOCAL_VERD[0] && POLSADOR_LOCAL_ROIG[0] && post_mode_configuracio)
      {
        post_mode_configuracio = false;
      }
      if (POLSADOR_LOCAL_ROIG[0] && POLSADOR_LOCAL_VERD[0] && !post_mode_configuracio) // Apretem dos botons per sortir config
      {
        temps_set_config_post = millis();
        post_mode_configuracio = true;
        if (debug)
        {
          Serial.println("ENTREM EN POST CONFIGURACIO MODE");
        }
      }
    }
    if (post_mode_configuracio && (millis() >= (temps_config + temps_set_config_post)))
    {
      LED_LOCAL_ROIG = false;
      LED_LOCAL_VERD = false;
      escriure_leds();
      funcio_local_num = sel;
      escriure_display_1(sel + 1); // Escribim la funció local
      escriure_display_2(0);       // Borrem linea inferior
      mode_configuracio = false;
      if (debug)
      {
        Serial.println("SORTIM CONFIGURACIO MODE");
        Serial.println("APAGUEM LEDS");
        Serial.print("Sel: ");
        Serial.println(sel);
      }
    }
  }
  LOCAL_CHANGE = false;
  post_mode_configuracio = false;
}

void detectar_mode_configuracio()
{
  if (LOCAL_CHANGE)
  {
    if (POLSADOR_LOCAL_ROIG[0] && POLSADOR_LOCAL_VERD[0] && !pre_mode_configuracio)
    {
      // Tenim els dos polsadors apretats i no estem en pre_mode_configuracio
      // Entrarem al mode CONFIG
      temps_set_config = millis();  // Llegim el temps actual per entrar a mode config
      pre_mode_configuracio = true; // Situem el flag en pre-mode-confi
      if (debug)
      {
        Serial.println("PRE CONFIGURACIO MODE");
      }
    }
    if (POLSADOR_LOCAL_ROIG[0] && POLSADOR_LOCAL_VERD[0] && (millis() >= (temps_config + temps_set_config)))
    {
      LED_LOCAL_ROIG = true;
      LED_LOCAL_VERD = true;
      escriure_leds();
    }
    if ((!POLSADOR_LOCAL_ROIG[0] || !POLSADOR_LOCAL_VERD[0]) && pre_mode_configuracio)
    { // Si deixem de pulsar polsadors i estavem en pre_mode_de_configuracio
      if ((millis()) >= (temps_config + temps_set_config))
      {                                // Si ha pasat el temps d'activació
        mode_configuracio = true;      // Entrem en mode configuracio
        pre_mode_configuracio = false; // Sortim del mode preconfiguracio
        LOCAL_CHANGE = false;
        if (debug)
        {
          Serial.println("CONFIGURACIO MODE");
        }
        Menu_configuracio();
      }
      else
      {
        pre_mode_configuracio = false; // Cancelem la preconfiguracio
        mode_configuracio = false;     // Cancelem la configuracio
        if (debug)
        {
          Serial.println("CANCELEM CONFIGURACIO MODE");
        }
      }
    }
  }
}

void initESP_NOW()
{
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
}

void setup()
{
  // Initialize Serial Monitor
  Serial.begin(115200);

  lcd.init();      // Inicialitzem lcd
  lcd.backlight(); // Arrenquem la llum de fons lcd
  lcd.clear();     // Esborrem la pantalla

  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);

  // Configurem els pins POLSADORS/LEDS
  pinMode(POLSADOR_ROIG_PIN, INPUT_PULLUP);
  pinMode(POLSADOR_VERD_PIN, INPUT_PULLUP);
  pinMode(LED_ROIG_PIN, OUTPUT);
  pinMode(LED_VERD_PIN, OUTPUT);

  Serial.println("TALLY MASTER");
  Serial.print("Versio: ");
  Serial.println(VERSIO);
  Serial.print("Server MAC Address:  ");
  Serial.println(WiFi.macAddress());

  lcd.setCursor(0, 0); // Situem cursor primer caracter, primera linea
  lcd.print("TALLY MASTER");
  lcd.setCursor(0, 1); // Primer caracter, segona linea
  lcd.print("Versio: ");
  lcd.setCursor(9, 1); // Caracter 9, segona linea
  lcd.print(VERSIO);

  // Set the device as a Station and Soft Access Point simultaneously
  WiFi.mode(WIFI_AP_STA);
  // Set device as a Wi-Fi Station
  // Haurem de veure que passa si no te una connexió wifi

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Setting as a Wi-Fi Station..");
  }

  Serial.print("Server SOFT AP MAC Address:  ");
  Serial.println(WiFi.softAPmacAddress());

  chan = WiFi.channel();
  Serial.print("Station IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Wi-Fi Channel: ");
  Serial.println(WiFi.channel());

  initESP_NOW(); // Iniciem el EspNow

  // Start Web server
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html); });

  // Events
  events.onConnect([](AsyncEventSourceClient *client)
                   {
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 10000); });
  server.addHandler(&events);

  // start server
  server.begin();
  GPEXTA.begin(); // Arrenquem el modul extra de GPIO A
  GPEXTB.begin(); // Arrenquem el modul extra de GPIO B

  for (int i = 0; i < 8; i++) // Definim els 8 primers bits com INPUT PULLUP
  {
    GPEXTA.pinMode(i, INPUT_PULLUP);
    GPEXTB.pinMode(i, INPUT_PULLUP);
  }

  for (int i = 8; i < 16; i++) // Definim els 8 ultims bits com OUTPUT
  {
    GPEXTA.pinMode(i, OUTPUT);
    GPEXTB.pinMode(i, OUTPUT);
  }
  lcd.clear();
  escriure_display_1(funcio_local_num + 1);
  last_time_roig = millis(); // Debouncer polsador
  last_time_verd = millis(); // Debouncer polsador
}

void loop()
{
  llegir_polsadors(); // Llegeix el valor dels polsadors
  if (LOCAL_CHANGE)
  {                               // Si hem apretat algun polsador
    detectar_mode_configuracio(); // Mirem si volem entrar en mode configuracio
    logica_polsadors_locals();    // Apliquem la lógica polsadors locals
  }
  llegir_gpi(); // Llegim els gpi CAL TORNAR A ACTIVAR
  if (GPIA_CHANGE || GPIB_CHANGE)
  {
    logica_gpi();
    escriure_leds();
    // Preparar color local matrix
    escriure_matrix(color_matrix[funcio_local_num]);
    // Preparar variables display
    escriure_display_2(display_text_2[funcio_local_num]); // TODO Passar la hora
    // data TALLY SEND
    comunicar_slaves(); // Enviem les dades rebudes als slaves
  }

  static unsigned long lastEventTime = millis();
  static const unsigned long EVENT_INTERVAL_MS = 5000; // Envia cada 5 segons informació
  // Cal canviar el loop per fer-lo quan es rebi un GPIO
  if ((millis() - lastEventTime) > EVENT_INTERVAL_MS)
  {
    events.send("ping", NULL, millis()); // Actualitza la web
    lastEventTime = millis();
    readDataToSend();
    esp_now_send(NULL, (uint8_t *)&outgoingSetpoints, sizeof(outgoingSetpoints)); // Envia valors master
  }
}
