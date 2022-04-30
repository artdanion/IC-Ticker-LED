#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager       version = 2.0.3-alpha
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <font_reverse.h>
#include "Font_Data_Numeric.h"

#define ESP32_RTOS
#define LEFTTORIGHT 0 // display orientation

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 12
#define CLK_PIN 18  // or SCK
#define DATA_PIN 23 // or MOSI
#define CS_PIN 5    // or SS

#define LED_PIN 27
#define NUMPIXELS 8
#define BUTTON_RED 14
#define BUTTON_BLUE 4

IPAddress server(158, 255, 212, 248);

#define PRINT(s, v)  // Print a string followed by a value (decimal)
#define PRINTX(s, v) // Print a string followed by a value (hex)
#define PRINTB(s, v) // Print a string followed by a value (binary)
#define PRINTC(s, v) // Print a string followed by a value (char)
#define PRINTS(s)    // Print a string

#if (defined ESP32_RTOS) && (defined ESP32)
void ota_handle(void *parameter)
{
  for (;;)
  {
    ArduinoOTA.handle();
    delay(3500);
  }
}
#endif

void setupOTA_Wifi(const char *nameprefix, const char *portalpw);
void reconnect();
void callback(char *topic, byte *payload, unsigned int length);
void printLocalTime();
void reverseString(char *original, char *reverse, int size);
void getTimeFromServer();
void displayTime();
void displayOnlyTime();
uint8_t utf8Ascii(uint8_t ascii);
void utf8AsciiConvert(char *src, char *des);

WiFiManager wifiManager;
MD_Parola mx = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); // SPI hardware interface
// MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel smile(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --------------------
// Constant parameters
//
#define ANIMATION_DELAY 150 // milliseconds
#define MAX_FRAMES 4        // number of animation frames
#define MSG_SIZE 100

// ========== General Variables ===========
//
const uint8_t pacman[MAX_FRAMES][18] = // ghost pursued by a pacman
    {
        {0xfe, 0x73, 0xfb, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0x7e, 0xff, 0xe7, 0xc3, 0x81, 0x00},
        {0xfe, 0x7b, 0xf3, 0x7f, 0xfb, 0x73, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xe7, 0xe7, 0x42, 0x00},
        {0xfe, 0x73, 0xfb, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xff, 0xe7, 0x66, 0x24},
        {0xfe, 0x7b, 0xf3, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xff, 0xff, 0x7e, 0x3c},
};
const uint8_t DATA_WIDTH = (sizeof(pacman[0]) / sizeof(pacman[0][0]));

uint32_t prevTimeAnim = 0; // remember the millis() value in animations
int16_t idx;               // display index (column)
uint8_t frame;             // current animation frame
uint8_t deltaFrame;        // the animation frame offset for the next frame

uint32_t color;
int r = 0;
int g = 0;
int b = 0;

const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

String mainMSG = "QujochÖ Ticker";
String noWifiMSG = "NO WIFI visit ... AP: Q-Ticker ... PW: qujochoe ... IP: 192.168.4.1";
String defMSG = "QujochÖ Ticker";

const char *timezone = "CET-1CEST,M3.5.0/02,M10.5.0/03"; // = CET/CEST  --> for adjusting your local time zone see: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char *ntpServer = "europe.pool.ntp.org";           // pool.ntp.org       // server pool prefix "2." could be necessary with IPv6

struct tm tm;
extern "C" uint8_t sntp_getreachability(uint8_t); // shows reachability of NTP Server (value != 0 means server could be reached) see explanation in http://savannah.nongnu.org/patch/?9581#comment0:

#ifdef LOCAL_LANG
const char *const PROGMEM days[]{"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
const char *const PROGMEM months[]{"Jan", "Feb", "Mrz", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};
const char *const PROGMEM monthsXL[]{"Jänner", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
#else
const char *const PROGMEM days[]{"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const PROGMEM months[]{"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const PROGMEM monthsXL[]{"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
#endif

char disp_MSG[100];
char mqttMSG[100];
char timeRev[20];
char buf[100];
#define BUF_SIZE 100
char curMessage[BUF_SIZE] = {""};
char newMessage[BUF_SIZE] = {"Hello! Enter new message?"};
char timeshow[10]; // array for time shown on display
char dateshow[30]; // array for date shown on display
bool newMessageAvailable = true;
uint8_t enableTime = 1;
uint8_t enableDate = 1;
char timeWeekDay[10];
char dayAfterTomorrow[12];
unsigned long previouscall = 0;
const char msgOrdinalNumber[] PROGMEM = ".";

// ========== Control routines ===========
//
// void resetMatrix(void)
// {
//   mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/2);
//   mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
//   mx.clear();
// }

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_BLUE, INPUT_PULLUP);

  client.setServer(server, 1883);
  client.setCallback(callback);

  mx.begin();
  mx.setPause(5000);

  if (LEFTTORIGHT)
  {
    mx.setFont(UpsideFont);
    noWifiMSG.toCharArray(buf, noWifiMSG.length() + 1);
    reverseString(buf, disp_MSG, noWifiMSG.length() + 1);

    defMSG.toCharArray(buf, defMSG.length() + 1);
    reverseString(buf, disp_MSG, defMSG.length() + 1);
  }
  else
  {
    noWifiMSG.toCharArray(disp_MSG, noWifiMSG.length() + 1);
  }
  Serial.print("No Wifi msg! ");
  Serial.println(disp_MSG);

  utf8AsciiConvert(disp_MSG, disp_MSG);

  mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  setupOTA_Wifi("Q-Ticker", "qujochoe"); // Name-xxxx.local PW for Portal

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  if (LEFTTORIGHT)
  {
    defMSG.toCharArray(buf, defMSG.length() + 1);
    reverseString(buf, disp_MSG, defMSG.length() + 1);
  }
  else
  {
    defMSG.toCharArray(disp_MSG, defMSG.length() + 1);
  }

  utf8AsciiConvert(disp_MSG, disp_MSG);

  prevTimeAnim = millis();

  smile.begin();
  smile.show();
  smile.setBrightness(80);
  color = smile.Color(155, 155, 155);
}

void loop()
{
#if (defined ESP32_RTOS) && (defined ESP32)
#else // If you do not use FreeRTOS, you have to regulary call the handle method.
  ArduinoOTA.handle();
#endif

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  for (int i = 0; i < NUMPIXELS; i++)
  {
    smile.setPixelColor(i, color);
  }
  smile.show();

  if (!digitalRead(BUTTON_BLUE))
  {
    Serial.println("Pressed BLUE");
    color = smile.Color(0, 0, 200);
  }
  if (!digitalRead(BUTTON_RED))
  {
    Serial.println("Pressed RED");
    color = smile.Color(200, 0, 0);
  }

  if (millis() - previouscall > (60 * 60000) && mx.displayAnimate())
  { // getting new news data from server and synchronising time every 60 minutes
    getTimeFromServer();
    previouscall = millis();
  }

  if (mx.displayAnimate())
  {
    if (newMessageAvailable)
    {
      mx.displayReset();
      mx.displayText(curMessage, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      newMessageAvailable = false;
    }
    else
    {
      mx.displayReset();
      mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
  }

  // static boolean bInit = true;  // initialise the animation

  // // Is it time to animate?
  // if (millis()-prevTimeAnim < ANIMATION_DELAY)
  //   return;
  // prevTimeAnim = millis();      // starting point for next time

  // mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  // // Initialize
  // if (bInit)
  // {
  //   mx.clear();
  //   idx = -DATA_WIDTH;
  //   frame = 0;
  //   deltaFrame = 1;
  //   bInit = false;

  //   // Lay out the dots
  //   for (uint8_t i=0; i<MAX_DEVICES; i++)
  //   {
  //     mx.setPoint(3, (i*COL_SIZE) + 3, true);
  //     mx.setPoint(4, (i*COL_SIZE) + 3, true);
  //     mx.setPoint(3, (i*COL_SIZE) + 4, true);
  //     mx.setPoint(4, (i*COL_SIZE) + 4, true);
  //   }
  // }

  // // clear old graphic
  // for (uint8_t i=0; i<DATA_WIDTH; i++)
  //   mx.setColumn(idx-DATA_WIDTH+i, 0);
  // // move reference column and draw new graphic
  // idx++;
  // for (uint8_t i=0; i<DATA_WIDTH; i++)
  //   mx.setColumn(idx-DATA_WIDTH+i, pacman[frame][i]);

  // // advance the animation frame
  // frame += deltaFrame;
  // if (frame == 0 || frame == MAX_FRAMES-1)
  //   deltaFrame = -deltaFrame;

  // // check if we are completed and set initialise for next time around
  // bInit = (idx == mx.getColumnCount()+DATA_WIDTH);

  // mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  return;
}

void setupOTA_Wifi(const char *nameprefix, const char *portalpw)
{
  // Configure the hostname
  uint16_t maxlen = strlen(nameprefix) + 7;
  char *fullhostname = new char[maxlen];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(fullhostname, maxlen, "%s-%02x%02x%02x", nameprefix, mac[3], mac[4], mac[5]);

  // wifiManager.resetSettings();
  // SPIFFS.format();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(fullhostname);

  if (!wifiManager.autoConnect(nameprefix, portalpw))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    // reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  Serial.println("local ip  ");
  Serial.print(WiFi.localIP());
  Serial.println();
  Serial.println(fullhostname);
  delete[] fullhostname;

  Serial.println("Done");

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232); // Use 8266 port if you are working in Sloeber IDE, it is fixed there and not adjustable

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]()
                     {
	//NOTE: make .detach() here for all functions called by Ticker.h library - not to interrupt transfer process in any way.
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type); });

  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });

  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("\nAuth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("\nBegin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("\nConnect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("\nReceive Failed");
    else if (error == OTA_END_ERROR) Serial.println("\nEnd Failed"); });

  ArduinoOTA.begin();

  Serial.println("OTA Initialized");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

#if defined(ESP32_RTOS) && defined(ESP32)
  xTaskCreate(
      ota_handle,   /* Task function. */
      "OTA_HANDLE", /* String with name of task. */
      10000,        /* Stack size in bytes. */
      NULL,         /* Parameter passed as input of the task */
      1,            /* Priority of the task. */
      NULL);        /* Task handle. */
#endif
}

void reconnect()
{
  // Loop until we're reconnected
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("Q-Ticker"))
    {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("devlol/test", "Q-Ticker");
      // ... and resubscribe
      client.subscribe("devlol/IoTlights/color");
      client.subscribe("devlol/QTicker");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  int count = 0;
  char msgIn[15];
  char mqttMSG[BUF_SIZE];

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  if (strcmp(topic, "devlol/IoTlights/color") == 0)
  {
    for (int i = 0; i < length; i++)
    {
      Serial.print((char)payload[i]);
      msgIn[i] = (char)payload[i];
      count++;
    }
    msgIn[count + 1] = '/0';
    Serial.println();
    count = 0;

    color = strtoul(msgIn + 1, 0, 16);
  }
  else if (strcmp(topic, "devlol/QTicker") == 0)
  {
    for (int i = 0; i < length; i++)
    {
      mqttMSG[i] = (char)payload[i];
      count++;
    }
    mqttMSG[count + 1] = '/0';
    strcpy(curMessage, mqttMSG);
    utf8AsciiConvert(curMessage, curMessage);
    newMessageAvailable = true;
    count = 0;
  }
  else
    return;

  Serial.println();
}

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  // Serial.print("Day of week: ");
  // Serial.println(&timeinfo, "%A");
  // Serial.print("Month: ");
  // Serial.println(&timeinfo, "%B");
  // Serial.print("Day of Month: ");
  // Serial.println(&timeinfo, "%d");
  // Serial.print("Year: ");
  // Serial.println(&timeinfo, "%Y");
  // Serial.print("Hour: ");
  // Serial.println(&timeinfo, "%H");
  // Serial.print("Hour (12 hour format): ");
  // Serial.println(&timeinfo, "%I");
  // Serial.print("Minute: ");
  // Serial.println(&timeinfo, "%M");
  // Serial.print("Second: ");
  // Serial.println(&timeinfo, "%S");

  // Serial.println("Time variables");
  // char timeHour[3];
  // strftime(timeHour, 3, "%H", &timeinfo);
  // Serial.println(timeHour);

  strftime(timeWeekDay, 10, "%A", &timeinfo);
  // Serial.println(timeWeekDay);
  // Serial.println();
}

void reverseString(char *original, char *reverse, int size)
{
  if (size > 0 && original != NULL && reverse != NULL)
  {
    for (int i = 0; i < size; ++i)
    {
      reverse[i] = original[size - i - 2];
    }

    reverse[size - 1] = '\0';
  }
}

uint8_t utf8Ascii(uint8_t ascii)
{ // See https://forum.arduino.cc/index.php?topic=171056.msg4457906#msg4457906
  // and http://playground.arduino.cc/Main/Utf8ascii
  static uint8_t cPrev;
  static uint8_t cPrePrev;
  uint8_t c = '\0';

  if (ascii < 0x7f)
  {
    cPrev = '\0';    // last character
    cPrePrev = '\0'; // penultimate character
    c = ascii;
  }
  else
  {
    switch (cPrev)
    {
    case 0xC2:
      c = ascii;
      break;
    case 0xC3:
      c = ascii | 0xC0;
      break;
    }
    switch (cPrePrev)
    {

    case 0xE2:
      if (cPrev == 0x82 && ascii == 0xAC)
        c = 128; // EURO SYMBOL          // 128 -> number in defined font table
      if (cPrev == 0x80 && ascii == 0xA6)
        c = 133; // horizontal ellipsis
      if (cPrev == 0x80 && ascii == 0x93)
        c = 150; // en dash
      if (cPrev == 0x80 && ascii == 0x9E)
        c = 132; // DOUBLE LOW-9 QUOTATION MARK
      if (cPrev == 0x80 && ascii == 0x9C)
        c = 147; // LEFT DOUBLE QUOTATION MARK
      if (cPrev == 0x80 && ascii == 0x9D)
        c = 148; // RIGHT DOUBLE QUOTATION MARK
      if (cPrev == 0x80 && ascii == 0x98)
        c = 145; // LEFT SINGLE QUOTATION MARK
      if (cPrev == 0x80 && ascii == 0x99)
        c = 146; // RIGHT SINGLE QUOTATION MARK

      break;
    }

    cPrePrev = cPrev; // save penultimate character
    cPrev = ascii;    // save last character
  }
  return (c);
}

void utf8AsciiConvert(char *src, char *des) // converts array form source array "src" to destination array "des"
{
  int k = 0;
  char c;
  for (int i = 0; src[i]; i++)
  {
    c = utf8Ascii(src[i]);
    if (c != '\0') // if (c!=0)
      des[k++] = c;
  }
  des[k] = '\0'; // des[k]=0;
}

//// TIME AND DATE FUNCTIONS ////

void getTimeFromServer()
{
  uint8_t time_retry = 0; // Counter retry counts time server
#ifdef ESP32
  configTzTime(timezone, ntpServer); // adjust your local time zone with variable timezone
#endif
  struct tm initial; // temp struct for checking if year==1970 (no received time information means year is 1970)
  initial.tm_year = 70;

  while (initial.tm_year == 70 && time_retry < 15)
  {
#ifdef ESP32 // get time from NTP server (ESP32)
    getLocalTime(&initial);
#else // get time from NTP server (ESP8266)
    if (esp8266::coreVersionNumeric() >= 20700000)
    {
      configTime(timezone, ntpServer);
    }
    else
    { // compatibility with ESP8266 Arduino Core Versions < 2.7.0
      setenv("TZ", timezone, 1);
      configTime(0, 0, ntpServer);
    }
#endif
    delay(500);
    time_t now = time(&now);
    localtime_r(&now, &initial);
#ifdef DEBUG
    Serial.print("Time Server connection attempt: ");
    Serial.println(time_retry + 1);
    Serial.print("current year: ");
    Serial.println(1900 + initial.tm_year);
#endif
    time_retry++;
  }

  if (time_retry >= 15)
  {
#ifdef DEBUG
    Serial.println("Connection to time server failed");
#endif
  }
  else
  {
    time_t now = time(&now);
    localtime_r(&now, &tm);
    if (enableTime == 1)
    {
      strftime(timeshow, sizeof(timeshow), "%H:%M", &tm);
#ifdef DEBUG
      Serial.print("Successfully requested current time from server: ");
      Serial.println(timeshow);
#endif
    }
  }
}

void makeDate()
{
  char buf1[20];
  char buf2[20];
  char buf3[20];
  char buf4[20];
  uint8_t weekday;

  time_t now = time(&now);
  localtime_r(&now, &tm);

  if (enableDate == 1)
  {
#ifdef SHORTDATE
    strftime(buf1, sizeof(buf1), "%e", &tm);               // see: http://www.cplusplus.com/reference/ctime/strftime/
    snprintf(buf2, sizeof(buf2), "%s", months[tm.tm_mon]); // see: http://www.willemer.de/informatik/cpp/timelib.htm
    strftime(buf3, sizeof(buf3), "%Y", &tm);
    snprintf(dateshow, sizeof(dateshow), "%s%s %s %s", buf1, msgOrdinalNumber, buf2, buf3); // Example: 1 Mar 2020 / 1. Mrz 2020
#else
    snprintf(buf1, sizeof(buf1), "%s", days[tm.tm_wday]);
    strftime(buf2, sizeof(buf2), "%e", &tm);
    snprintf(buf3, sizeof(buf3), "%s", monthsXL[tm.tm_mon]);
    strftime(buf4, sizeof(buf4), "%Y", &tm);
    snprintf(dateshow, sizeof(dateshow), "%s %s%s %s %s", buf1, buf2, msgOrdinalNumber, buf3, buf4); // Example: Sunday 1 March 2020 / Sonntag 1. März 2020
    utf8AsciiConvert(dateshow, dateshow);                                                            // conversion necessary for "Jänner" and "März"
#endif
  }

  weekday = tm.tm_wday;
  switch (weekday)
  {
  case 0:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[2]); // if today is Sunday, day after tomorrow is Tuesday
    break;
  case 1:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[3]);
    break;
  case 2:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[4]);
    break;
  case 3:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[5]);
    break;
  case 4:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[6]);
    break;
  case 5:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[0]);
    break;
  case 6:
    snprintf(dayAfterTomorrow, sizeof(dayAfterTomorrow), "%s", days[1]);
    break;
  }

#ifdef DEBUG
  Serial.print("Date: ");
  Serial.println(dateshow);
#endif
}

void displayTime()
{ // standard case: if messages other than time are activated
  static time_t lastminute = 0;
  time_t now = time(&now);
  localtime_r(&now, &tm);
  if (tm.tm_min != lastminute)
  {
    lastminute = tm.tm_min;
    if (enableTime == 1)
    {
      strftime(timeshow, sizeof(timeshow), "%H:%M", &tm);
#ifdef DEBUG
      Serial.print("current time: ");
      Serial.println(timeshow);
#endif
    }
  }

  if (tm.tm_hour == 0 && tm.tm_min == 0 && tm.tm_sec == 0)
    makeDate(); // at 0:00 make new date
}

void displayOnlyTime()
{ // special case: if only time message is activated -> hh:mm:ss
  mx.setTextAlignment(PA_CENTER);
  static time_t lastsecond = 0;
  time_t now = time(&now);
  localtime_r(&now, &tm);
  if (tm.tm_sec != lastsecond)
  {
    lastsecond = tm.tm_sec;
    if (enableTime == 1)
    {
      strftime(timeshow, sizeof(timeshow), "%H:%M:%S", &tm);
      mx.setFont(numeric7Seg);
      mx.displayReset();
      mx.print(timeshow);
    }
    else
    {
      mx.displayReset();
      mx.displayClear(); // display shows nothing
    }
  }

  if (tm.tm_hour == 0 && tm.tm_min == 0 && tm.tm_sec == 0)
    makeDate();
}
