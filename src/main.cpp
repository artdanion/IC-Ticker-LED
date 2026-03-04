#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <FastLED.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <font_reverse.h>
#include "Font_Data_Numeric.h"
#include "Parola_Fonts_data.h"

// #define ESP32_RTOS
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

#define BUF_SIZE 100

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
    delay(100); // Reduced from 3500ms to 100ms for reliable OTA response
  }
}
#endif

void setupOTA_Wifi(const char *nameprefix, const char *portalpw);
void reconnect();
void callback(char *topic, byte *payload, unsigned int length);
void printLocalTime();
void reverseString(char *original, char *reverse, int size);
uint8_t utf8Ascii(uint8_t ascii);
void utf8AsciiConvert(char *src, char *des);
void goToSleep();
void scrollAndWait(const char *msg, int speed = 20, int pause = 10);

WiFiManager wifiManager;
MD_Parola mx = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); // SPI hardware interface

WiFiClient espClient;
PubSubClient client(espClient);
CRGB leds[NUMPIXELS];

#define ANIMATION_DELAY 150
#define MAX_FRAMES 4
#define MSG_SIZE 100

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 32400       /* Time ESP32 will go to sleep (in seconds) --> 9h */

RTC_DATA_ATTR int bootCount = 0;

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

CRGB color;
int r = 0;
int g = 0;
int b = 0;

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

String mainMSG = "InterfaceCultures";
String noWifiMSG = "NO WIFI visit ... AP: IC-Ticker ... PW: interface ... IP: 192.168.4.1";
String defMSG = "IC Ticker";

const char *timezone = "CET-1CEST,M3.5.0/02,M10.5.0/03";
const char *ntpServer = "europe.pool.ntp.org";

struct tm tm;

char disp_MSG[BUF_SIZE];
char disp_MSG_2[BUF_SIZE];
char mqttMSG[BUF_SIZE];
char buf[BUF_SIZE];
char timeMSG[25];
char dateMSG[25];
char sleepTMR[10];

char curMessage[BUF_SIZE] = {""};
char newMessage[BUF_SIZE] = {"Hello! Enter new message?"};
char convTmp1[BUF_SIZE]; // global conversion temp buffers to avoid stack overflow
char convTmp2[BUF_SIZE];

unsigned long previouscall = 0;
int showAgain = 0;
bool newMessageAvailable = false; // no message yet on startup
bool change;
bool flip;

// Helper: scroll a message and block until it has fully scrolled once
void scrollAndWait(const char *msg, int speed, int pause)
{
  // Use global buffers to avoid stack overflow
  strncpy(convTmp1, msg, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, convTmp2);

  mx.displayReset();
  mx.setSpeed(speed);
  mx.setPause(pause);
  mx.setFont(ExtASCII);
  mx.displayText(convTmp2, PA_CENTER, speed, pause, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  while (!mx.displayAnimate())
    ;
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_RED, INPUT_PULLUP);
  pinMode(BUTTON_BLUE, INPUT_PULLUP);
  delay(500);

  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for " + String(TIME_TO_SLEEP) + " Seconds after 23:00");

  client.setServer(server, 1883);
  client.setCallback(callback);

  // Initialize FastLED early, before WiFi
  Serial.printf("Free heap before FastLED: %u bytes\n", ESP.getFreeHeap());
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUMPIXELS);
  FastLED.setBrightness(255); // max brightness for testing
  color = CRGB(255, 0, 0);    // bright red - easy to see
  fill_solid(leds, NUMPIXELS, color);
  FastLED.show();
  Serial.println("FastLED OK");

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
    mx.setFont(ExtASCII);
    noWifiMSG.toCharArray(disp_MSG, noWifiMSG.length() + 1);
  }
  Serial.print("No Wifi msg! ");
  Serial.println(disp_MSG);

  strncpy(convTmp1, disp_MSG, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, disp_MSG);
  mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  setupOTA_Wifi("IC-Ticker", "interface"); // connects WiFi and starts OTA

  // ---- Startup debug messages on the display ----
  char startupMsg[BUF_SIZE];
  char mqttStatus[BUF_SIZE];

  // Try MQTT once and store result string
  Serial.print("Attempting MQTT connection...");
  if (client.connect("IC-Ticker"))
  {
    client.publish("devlol/test", "IC-Ticker");
    client.publish("devlol/test", WiFi.localIP().toString().c_str());
    client.subscribe("devlol/IoTlights/color");
    client.subscribe("devlol/IC-Ticker");
    Serial.println("MQTT connected");
    snprintf(mqttStatus, BUF_SIZE, "MQTT: OK %s", server.toString().c_str());
  }
  else
  {
    Serial.printf("MQTT failed, rc=%d\n", client.state());
    snprintf(mqttStatus, BUF_SIZE, "MQTT FAIL rc=%d", client.state());
  }

  // Scroll WiFi, IP, MQTT status twice
  for (int repeat = 0; repeat < 2; repeat++)
  {
    snprintf(startupMsg, BUF_SIZE, "WiFi: %s", WiFi.SSID().c_str());
    scrollAndWait(startupMsg);

    snprintf(startupMsg, BUF_SIZE, "IP: %s", WiFi.localIP().toString().c_str());
    scrollAndWait(startupMsg);

    scrollAndWait(mqttStatus);
  }
  // ---- End startup messages ----

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
    mainMSG.toCharArray(disp_MSG_2, mainMSG.length() + 1);
  }

  // utf8AsciiConvert requires different src/dst buffers — use global temp copies
  strncpy(convTmp1, disp_MSG, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  strncpy(convTmp2, disp_MSG_2, BUF_SIZE - 1);
  convTmp2[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, disp_MSG);
  utf8AsciiConvert(convTmp2, disp_MSG_2);

  prevTimeAnim = millis();

  Serial.println("Setup done");
}

void loop()
{
#if (defined ESP32_RTOS) && (defined ESP32)
#else // If you do not use FreeRTOS, you have to regularly call the handle method.
  ArduinoOTA.handle();
#endif

  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  if (true) // FastLED always ready
  {
    fill_solid(leds, NUMPIXELS, color);
    FastLED.show();
  }

  if (!digitalRead(BUTTON_BLUE))
  {
    Serial.println("Pressed BLUE");
    color = CRGB(0, 0, 200);
  }
  if (!digitalRead(BUTTON_RED))
  {
    Serial.println("Pressed RED");
    color = CRGB(200, 0, 0);
  }

  if (millis() - previouscall > (60 * 1000) && mx.displayAnimate())
  {
    printLocalTime();
    mx.displayReset();
    mx.setSpeed(15);
    mx.setPause(1000);
    if (change)
    {
      mx.displayText(timeMSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
    else
    {
      mx.displayReset();
      mx.setSpeed(25);
      mx.setPause(10);
      mx.displayText(dateMSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
    change = !change;
    previouscall = millis();
    Serial.println(ESP.getFreeHeap());
  }

  if (mx.displayAnimate())
  {
    if (newMessageAvailable)
    {
      mx.displayReset();
      mx.setSpeed(25);
      mx.setPause(10);
      mx.displayText(curMessage, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      if (showAgain < 5)
      {
        showAgain++;
      }
      else
      {
        showAgain = 0;
        newMessageAvailable = false;
      }
    }
    else
    {
      if (!flip)
      {
        mx.displayReset();
        mx.setSpeed(15);
        mx.setPause(1000);
        mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      }
      else
      {
        mx.displayReset();
        mx.setSpeed(15);
        mx.setPause(1000);
        mx.displayText(disp_MSG_2, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      }
      flip = !flip;
    }
  }
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

  wifiManager.setAPCallback([](WiFiManager *wm)
                            {
    Serial.println("No WiFi - Config portal started");
    mx.displayReset();
    mx.setSpeed(20);
    mx.setPause(10);
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
      mx.setFont(ExtASCII);
      noWifiMSG.toCharArray(disp_MSG, noWifiMSG.length() + 1);
    }
    strncpy(convTmp1, disp_MSG, BUF_SIZE-1); convTmp1[BUF_SIZE-1]='\0'; utf8AsciiConvert(convTmp1, disp_MSG);
    mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);

    // Keep scrolling for as long as the portal is open (portal timeout is handled by WiFiManager)
    while (wm->getConfigPortalActive())
    {
      mx.displayAnimate();
      delay(1);
    } });

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(fullhostname);

  if (!wifiManager.autoConnect(nameprefix, portalpw))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  Serial.println("local ip  ");
  Serial.print(WiFi.localIP());
  Serial.println();
  Serial.println(fullhostname);
  delete[] fullhostname;

  Serial.println("Done");

  ArduinoOTA.setHostname("IC-Ticker-debug");

  ArduinoOTA.onStart([]()
                     {
  Serial.println("===== OTA START =====");
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
  Serial.printf("WiFi status: %d\n", WiFi.status());
  Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("RSSI: %d\n", WiFi.RSSI());

  String type;
  if (ArduinoOTA.getCommand() == U_FLASH)
    type = "sketch";
  else
    type = "filesystem";
  Serial.println("Updating " + type); });

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

  ArduinoOTA.setPassword("interface");

  ArduinoOTA.begin();

  Serial.println("OTA Initialized");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

#if defined(ESP32_RTOS) && defined(ESP32)
  xTaskCreate(
      ota_handle,   /* Task function. */
      "OTA_HANDLE", /* String with name of task. */
      20000,        /* Stack size in bytes. */
      NULL,         /* Parameter passed as input of the task */
      1,            /* Priority of the task. */
      NULL);        /* Task handle. */
#endif
}

void reconnect()
{
  if (client.connected())
    return;

  unsigned long now = millis();
  static unsigned long lastAttempt = 0;

  if (now - lastAttempt > 5000)
  {
    lastAttempt = now;
    Serial.print("Attempting MQTT connection...");
    if (client.connect("IC-Ticker"))
    {
      Serial.println("connected");
      client.publish("devlol/test", "IC-Ticker");
      IPAddress localIP = WiFi.localIP();
      client.publish("devlol/test", localIP.toString().c_str());
      client.subscribe("devlol/IoTlights/color");
      client.subscribe("devlol/IC-Ticker");
    }
    else
    {
      Serial.printf("failed, rc=%d, retry in 5s\n", client.state());
    }
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  int count = 0;
  char msgIn[15];

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
    msgIn[count] = '\0';
    Serial.println();
    count = 0;
    uint32_t hex = strtoul(msgIn + 1, 0, 16);
    color = CRGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
  }
  else if (strcmp(topic, "devlol/IC-Ticker") == 0)
  {
    String mqttBuff = "";
    if (length > 95)
      length = 95;
    for (int i = 0; i < length; i++)
    {
      mqttBuff = mqttBuff + (char)payload[i];
    }
    mqttBuff.toCharArray(curMessage, mqttBuff.length() + 1);
    strncpy(convTmp1, curMessage, BUF_SIZE - 1);
    convTmp1[BUF_SIZE - 1] = '\0';
    utf8AsciiConvert(convTmp1, curMessage);
    newMessageAvailable = true;
  }
  else
    return;

  Serial.println();
}

void goToSleep()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("goToSleep: failed to get time, skipping sleep");
    return;
  }

  int hour = timeinfo.tm_hour;

  // Sanity check: only sleep if actually outside operating hours
  if (hour >= 6 && hour < 23)
  {
    Serial.printf("goToSleep: called at %02d:%02d but hour is in operating range, skipping sleep\n", hour, timeinfo.tm_min);
    return;
  }

  int currentSeconds = hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
  int wakeSeconds = 6 * 3600; // wake at 06:00

  int sleepDuration = wakeSeconds - currentSeconds;
  if (sleepDuration <= 0)
    sleepDuration += 24 * 3600;

  Serial.printf("Sleeping for %d seconds until 06:00\n", sleepDuration);
  mx.displayClear();
  esp_sleep_enable_timer_wakeup((uint64_t)sleepDuration * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void printLocalTime()
{
  struct tm timeinfo;
  int hour = 0;

  // Wait up to 5 seconds for a valid NTP-synced time (year must be >= 2020)
  int retries = 0;
  do
  {
    if (!getLocalTime(&timeinfo))
    {
      Serial.println("Failed to obtain time");
      return;
    }
    if (timeinfo.tm_year >= 120)
      break; // tm_year is years since 1900, so 120 = 2020
    delay(500);
    retries++;
  } while (retries < 10);

  if (timeinfo.tm_year < 120)
  {
    Serial.println("NTP not synced yet, skipping sleep check");
    return;
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  strftime(dateMSG, sizeof(dateMSG), "%a, %B %d %Y", &timeinfo);
  strftime(timeMSG, sizeof(timeMSG), "%H:%M:%S", &timeinfo);
  strftime(sleepTMR, sizeof(sleepTMR), "%H", &timeinfo);

  hour = timeinfo.tm_hour;
  Serial.printf("Hour check: %d\n", hour);
  if (hour >= 23 || hour < 6)
  {
    goToSleep();
  }
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

void utf8AsciiConvert(char *src, char *des)
{
  int k = 0;
  char c;
  for (int i = 0; src[i]; i++)
  {
    c = utf8Ascii(src[i]);
    if (c != '\0')
      des[k++] = c;
    else
    {
      des[k] = '\0';
    }
  }
  des[k] = '\0';
}

uint8_t utf8Ascii(uint8_t ascii)
{
  static uint8_t cPrev;
  uint8_t c = '\0';

  PRINTX("\nutf8Ascii 0x", ascii);

  if (ascii < 0x7f)
  {
    cPrev = '\0';
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
    case 0x82:
      if (ascii == 0xAC)
        c = 0x80;
    case 0xE2:
      switch (ascii)
      {
      case 0x80:
        c = 133;
        break;
      }
      break;
    default:
      PRINTS("!Unhandled! ");
    }
    cPrev = ascii;
  }

  PRINTX(" -> 0x", c);

  return (c);
}

void utf8Ascii(char *s)
{
  uint8_t c;
  char *cp = s;

  PRINT("\nConverting: ", s);

  while (*s != '\0')
  {
    c = utf8Ascii(*s++);
    if (c != '\0')
      *cp++ = c;
  }
  *cp = '\0';
}
