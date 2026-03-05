// ============================================================
//  IC-Ticker — main.cpp
//  ESP32 + MD_MAX72xx matrix display + MQTT + OTA + FastLED
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>   // https://github.com/tzapu/WiFiManager
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


// ============================================================
//  Compile-time configuration
// ============================================================

// #define ESP32_RTOS           // uncomment to use FreeRTOS OTA task

#define LEFTTORIGHT 0           // 0 = normal orientation, 1 = upside-down

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   12
#define CLK_PIN       18        // SCK
#define DATA_PIN      23        // MOSI
#define CS_PIN         5        // SS

#define LED_PIN       27
#define NUMPIXELS      8
#define BUTTON_RED    14
#define BUTTON_BLUE    4

#define BUF_SIZE             100
#define WIFI_PAGE_INTERVAL  3000   // ms each portal info page is shown
#define ANIMATION_DELAY      150
#define MAX_FRAMES             4
#define MSG_SIZE             100

#define uS_TO_S_FACTOR  1000000ULL  // µs → s
#define TIME_TO_SLEEP     32400     // deep-sleep duration (s) = 9 h


// ============================================================
//  Stubs for optional debug macros (replace with real ones if needed)
// ============================================================
#define PRINT(s, v)
#define PRINTX(s, v)
#define PRINTB(s, v)
#define PRINTC(s, v)
#define PRINTS(s)


// ============================================================
//  Forward declarations
// ============================================================
void setup_Wifi(const char *nameprefix, const char *portalpw);
void OTA_setup();
void showWifiPage(uint8_t index);
void reconnect();
void callback(char *topic, byte *payload, unsigned int length);
void printLocalTime();
void goToSleep();
void scrollAndWait(const char *msg, int speed = 20, int pause = 10);
void reverseString(char *original, char *reverse, int size);
uint8_t utf8Ascii(uint8_t ascii);
void    utf8Ascii(char *s);
void    utf8AsciiConvert(char *src, char *des);

#if defined(ESP32_RTOS) && defined(ESP32)
void ota_handle(void *parameter);
#endif


// ============================================================
//  Hardware objects
// ============================================================
MD_Parola   mx = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
WiFiManager wifiManager;
WiFiClient  espClient;
PubSubClient client(espClient);
CRGB leds[NUMPIXELS];


// ============================================================
//  WiFi / OTA state
// ============================================================
bool          wifiPortalActive = false;
bool          otaInitialized   = false;
uint8_t       wifiPageIndex    = 0;
unsigned long wifiPageTimer    = 0;
String        wifiPages[3];


// ============================================================
//  Deep-sleep / boot
// ============================================================
RTC_DATA_ATTR int bootCount = 0;


// ============================================================
//  Display / animation
// ============================================================
const uint8_t pacman[MAX_FRAMES][18] =   // ghost + pacman sprite
{
  {0xfe,0x73,0xfb,0x7f,0xf3,0x7b,0xfe,0x00,0x00,0x00,0x3c,0x7e,0x7e,0xff,0xe7,0xc3,0x81,0x00},
  {0xfe,0x7b,0xf3,0x7f,0xfb,0x73,0xfe,0x00,0x00,0x00,0x3c,0x7e,0xff,0xff,0xe7,0xe7,0x42,0x00},
  {0xfe,0x73,0xfb,0x7f,0xf3,0x7b,0xfe,0x00,0x00,0x00,0x3c,0x7e,0xff,0xff,0xff,0xe7,0x66,0x24},
  {0xfe,0x7b,0xf3,0x7f,0xf3,0x7b,0xfe,0x00,0x00,0x00,0x3c,0x7e,0xff,0xff,0xff,0xff,0x7e,0x3c},
};
const uint8_t DATA_WIDTH = sizeof(pacman[0]) / sizeof(pacman[0][0]);

uint32_t prevTimeAnim = 0;
int16_t  idx;
uint8_t  frame;
uint8_t  deltaFrame;

unsigned long previouscall       = 0;
int           showAgain          = 0;
bool          newMessageAvailable = false;
bool          change             = false;
bool          flip               = false;


// ============================================================
//  MQTT / network
// ============================================================
IPAddress server(158, 255, 212, 248);


// ============================================================
//  Time / NTP
// ============================================================
const long gmtOffset_sec      = 3600;
const int  daylightOffset_sec = 3600;
const char *timezone  = "CET-1CEST,M3.5.0/02,M10.5.0/03";
const char *ntpServer = "europe.pool.ntp.org";
struct tm tm;


// ============================================================
//  LED colour
// ============================================================
CRGB color;
int r = 0, g = 0, b = 0;


// ============================================================
//  Display message strings
// ============================================================
String mainMSG    = "InterfaceCultures";
String noWifiMSG  = "NO WIFI";
String defMSG     = "IC Ticker";


// ============================================================
//  Character buffers  (globals avoid stack overflow)
// ============================================================
char disp_MSG  [BUF_SIZE];
char disp_MSG_2[BUF_SIZE];
char mqttMSG   [BUF_SIZE];
char buf       [BUF_SIZE];
char timeMSG   [25];
char dateMSG   [25];
char sleepTMR  [10];
char curMessage[BUF_SIZE] = {""};
char newMessage[BUF_SIZE] = {"Hello! Enter new message?"};
char convTmp1  [BUF_SIZE];   // temp buffers for utf8 conversion
char convTmp2  [BUF_SIZE];


// ============================================================
//  FreeRTOS OTA task  (only compiled when ESP32_RTOS defined)
// ============================================================
#if defined(ESP32_RTOS) && defined(ESP32)
void ota_handle(void *parameter)
{
  for (;;)
  {
    ArduinoOTA.handle();
    delay(100);
  }
}
#endif


// ============================================================
//  UTF-8 → ASCII helpers
// ============================================================
uint8_t utf8Ascii(uint8_t ascii)
{
  static uint8_t cPrev;
  uint8_t c = '\0';

  if (ascii < 0x7f)
  {
    cPrev = '\0';
    c = ascii;
  }
  else
  {
    switch (cPrev)
    {
      case 0xC2: c = ascii;        break;
      case 0xC3: c = ascii | 0xC0; break;
      case 0x82: if (ascii == 0xAC) c = 0x80; break;
      case 0xE2:
        if (ascii == 0x80) c = 133;
        break;
      default: break;
    }
    cPrev = ascii;
  }
  return c;
}

void utf8Ascii(char *s)
{
  uint8_t c;
  char *cp = s;
  while (*s != '\0')
  {
    c = utf8Ascii(*s++);
    if (c != '\0') *cp++ = c;
  }
  *cp = '\0';
}

void utf8AsciiConvert(char *src, char *des)
{
  int k = 0;
  for (int i = 0; src[i]; i++)
  {
    char c = utf8Ascii(src[i]);
    if (c != '\0') des[k++] = c;
    else           des[k]   = '\0';
  }
  des[k] = '\0';
}


// ============================================================
//  String helpers
// ============================================================
void reverseString(char *original, char *reverse, int size)
{
  if (size > 0 && original && reverse)
  {
    for (int i = 0; i < size; ++i)
      reverse[i] = original[size - i - 2];
    reverse[size - 1] = '\0';
  }
}


// ============================================================
//  Display helpers
// ============================================================

// Block until one full scroll cycle completes
void scrollAndWait(const char *msg, int speed, int pause)
{
  strncpy(convTmp1, msg, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, convTmp2);

  mx.displayReset();
  mx.setSpeed(speed);
  mx.setPause(pause);
  mx.setFont(ExtASCII);
  mx.displayText(convTmp2, PA_CENTER, speed, pause, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!mx.displayAnimate()) ;
}

// Show one of the three portal-info pages (AP / PW / IP) statically
void showWifiPage(uint8_t index)
{
  mx.displayReset();

  String msg = wifiPages[index];

  if (LEFTTORIGHT)
  {
    mx.setFont(UpsideFont);
    msg.toCharArray(buf, msg.length() + 1);
    reverseString(buf, disp_MSG, msg.length() + 1);
  }
  else
  {
    mx.setFont(ExtASCII);
    msg.toCharArray(disp_MSG, msg.length() + 1);
  }

  strncpy(convTmp1, disp_MSG, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, disp_MSG);

  mx.displayText(disp_MSG, PA_CENTER, 0, 0, PA_PRINT, PA_NO_EFFECT);
  mx.displayAnimate();
}


// ============================================================
//  OTA setup  — call only AFTER WiFi is connected
// ============================================================
void OTA_setup()
{
  ArduinoOTA.setHostname("IC-Ticker-debug");
  ArduinoOTA.setPassword("interface");

  ArduinoOTA.onStart([]()
  {
    Serial.println("===== OTA START =====");
    Serial.printf("Free heap:   %u\n", ESP.getFreeHeap());
    Serial.printf("WiFi status: %d\n", WiFi.status());
    Serial.printf("Local IP:    %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("RSSI:        %d\n", WiFi.RSSI());
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Updating " + type);
  });

  ArduinoOTA.onEnd([]()
  {
    Serial.println("\nOTA End");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error)
  {
    Serial.printf("OTA Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR)    Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)     Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  otaInitialized = true;

  Serial.println("OTA initialized");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

#if defined(ESP32_RTOS) && defined(ESP32)
  xTaskCreate(ota_handle, "OTA_HANDLE", 20000, NULL, 1, NULL);
#endif
}


// ============================================================
//  WiFi setup  — non-blocking WiFiManager
//
//  • If saved credentials exist  → connects immediately, calls OTA_setup()
//  • If no saved credentials      → starts AP + portal, sets wifiPortalActive
//    loop() then drives portal processing and page cycling until connected
// ============================================================
void setup_Wifi(const char *nameprefix, const char *portalpw)
{
  // Build hostname: <prefix>-AABBCC
  uint16_t maxlen = strlen(nameprefix) + 7;
  char *fullhostname = new char[maxlen];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(fullhostname, maxlen, "%s-%02x%02x%02x", nameprefix, mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(fullhostname);
  delete[] fullhostname;

  // wifiManager.resetSettings();  // ← uncomment to wipe saved credentials for testing

  wifiManager.setConnectRetries(10);
  wifiManager.setCleanConnect(true);
  wifiManager.setConfigPortalBlocking(false); // non-blocking: autoConnect() returns immediately
  wifiManager.setTimeout(900);                // portal timeout: 15 min (900 s)
  wifiManager.setDarkMode(true);
  wifiManager.setDebugOutput(true);

  wifiManager.setSaveConfigCallback([]()
  {
    Serial.println("WiFi credentials saved, connecting...");
  });

  // AP callback: fires when no saved WiFi found.
  // Just set state + show first page, then RETURN immediately.
  // autoConnect() / loop() handle everything from here.
  wifiManager.setAPCallback([portalpw](WiFiManager *wm)
  {
    Serial.println("No saved WiFi — config portal started");
    Serial.printf("  SSID : %s\n", wm->getConfigPortalSSID().c_str());
    Serial.printf("  PW   : %s\n", portalpw);
    Serial.println("  IP   : 192.168.4.1");

    wifiPortalActive = true;
    wifiPageIndex    = 0;
    wifiPageTimer    = millis();

    wifiPages[0] = "AP: " + wm->getConfigPortalSSID();
    wifiPages[1] = "PW: " + String(portalpw);
    wifiPages[2] = "IP: 192.168.4.1";

    showWifiPage(0);
  });

  // Non-blocking: returns immediately regardless of outcome
  wifiManager.autoConnect(nameprefix, portalpw);

  // Already connected (saved credentials found)?
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connected (saved credentials)");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    OTA_setup();
    // wifiPortalActive stays false — loop() skips portal block
  }
  // else: portal is running, wifiPortalActive == true, loop() takes over
}


// ============================================================
//  MQTT
// ============================================================
void reconnect()
{
  if (client.connected()) return;

  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < 5000) return;
  lastAttempt = millis();

  Serial.print("Attempting MQTT connection...");
  if (client.connect("IC-Ticker"))
  {
    Serial.println("connected");
    client.publish("devlol/test", "IC-Ticker");
    client.publish("devlol/test", WiFi.localIP().toString().c_str());
    client.subscribe("devlol/IoTlights/color");
    client.subscribe("devlol/IC-Ticker");
  }
  else
  {
    Serial.printf("failed rc=%d, retry in 5 s\n", client.state());
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.printf("MQTT [%s] ", topic);

  if (strcmp(topic, "devlol/IoTlights/color") == 0)
  {
    char msgIn[15] = {0};
    int  count = min((unsigned int)14, length);
    for (int i = 0; i < count; i++) msgIn[i] = (char)payload[i];
    msgIn[count] = '\0';
    Serial.println(msgIn);
    uint32_t hex = strtoul(msgIn + 1, nullptr, 16);
    color = CRGB((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
  }
  else if (strcmp(topic, "devlol/IC-Ticker") == 0)
  {
    if (length > 95) length = 95;
    String mqttBuff = "";
    for (unsigned int i = 0; i < length; i++) mqttBuff += (char)payload[i];
    mqttBuff.toCharArray(curMessage, mqttBuff.length() + 1);
    strncpy(convTmp1, curMessage, BUF_SIZE - 1);
    convTmp1[BUF_SIZE - 1] = '\0';
    utf8AsciiConvert(convTmp1, curMessage);
    newMessageAvailable = true;
    Serial.println(curMessage);
  }
  else
  {
    Serial.println("(unknown topic, ignored)");
  }
}


// ============================================================
//  Time / NTP / deep sleep
// ============================================================
void goToSleep()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("goToSleep: failed to get time, skipping");
    return;
  }

  int hour = timeinfo.tm_hour;
  if (hour >= 6 && hour < 23)
  {
    Serial.printf("goToSleep: hour %02d is in operating range, skipping\n", hour);
    return;
  }

  int currentSeconds = hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
  int wakeSeconds    = 6 * 3600; // wake at 06:00
  int sleepDuration  = wakeSeconds - currentSeconds;
  if (sleepDuration <= 0) sleepDuration += 24 * 3600;

  Serial.printf("Sleeping %d s until 06:00\n", sleepDuration);
  mx.displayClear();
  esp_sleep_enable_timer_wakeup((uint64_t)sleepDuration * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void printLocalTime()
{
  struct tm timeinfo;

  // Wait up to 5 s for a valid NTP-synced time (tm_year >= 120 → year >= 2020)
  for (int retries = 0; retries < 10; retries++)
  {
    if (!getLocalTime(&timeinfo)) { Serial.println("Failed to obtain time"); return; }
    if (timeinfo.tm_year >= 120)  break;
    delay(500);
  }

  if (timeinfo.tm_year < 120)
  {
    Serial.println("NTP not synced yet, skipping time display");
    return;
  }

  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  strftime(dateMSG,   sizeof(dateMSG),   "%a, %B %d %Y", &timeinfo);
  strftime(timeMSG,   sizeof(timeMSG),   "%H:%M:%S",     &timeinfo);
  strftime(sleepTMR,  sizeof(sleepTMR),  "%H",           &timeinfo);

  Serial.printf("Hour check: %d\n", timeinfo.tm_hour);
  if (timeinfo.tm_hour >= 23 || timeinfo.tm_hour < 6)
    goToSleep();
}


// ============================================================
//  setup()
// ============================================================
void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_RED,  INPUT_PULLUP);
  pinMode(BUTTON_BLUE, INPUT_PULLUP);
  delay(500);

  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));
  Serial.println("Deep-sleep armed for " + String(TIME_TO_SLEEP) + " s after 23:00");

  // MQTT server
  client.setServer(server, 1883);
  client.setCallback(callback);

  // FastLED — initialise before WiFi to avoid timing issues
  Serial.printf("Free heap before FastLED: %u bytes\n", ESP.getFreeHeap());
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUMPIXELS);
  FastLED.setBrightness(255);
  color = CRGB(255, 0, 0);
  fill_solid(leds, NUMPIXELS, color);
  FastLED.show();
  Serial.println("FastLED OK");

  // Matrix display — show "NO WIFI" while connecting
  mx.begin();
  mx.setSpeed(20);
  mx.setPause(10);

  if (LEFTTORIGHT)
  {
    mx.setFont(UpsideFont);
    noWifiMSG.toCharArray(buf, noWifiMSG.length() + 1);
    reverseString(buf, disp_MSG, noWifiMSG.length() + 1);
  }
  else
  {
    mx.setFont(ExtASCII);
    noWifiMSG.toCharArray(disp_MSG, noWifiMSG.length() + 1);
  }

  strncpy(convTmp1, disp_MSG, BUF_SIZE - 1);
  convTmp1[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, disp_MSG);
  mx.displayText(disp_MSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  Serial.println("Display init OK, showing: " + String(disp_MSG));

  // WiFi + OTA  (non-blocking — loop() handles portal if needed)
  setup_Wifi("IC-Ticker", "interface");

  // If portal is active we must return now and let loop() drive it.
  // The code below only runs once WiFi is already connected.
  if (wifiPortalActive) return;

  // ---- Post-connect startup ----
  char startupMsg[BUF_SIZE];
  char mqttStatus[BUF_SIZE];

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
    Serial.printf("MQTT failed rc=%d\n", client.state());
    snprintf(mqttStatus, BUF_SIZE, "MQTT FAIL rc=%d", client.state());
  }

  // Scroll WiFi / IP / MQTT status twice
  for (int repeat = 0; repeat < 2; repeat++)
  {
    snprintf(startupMsg, BUF_SIZE, "WiFi: %s", WiFi.SSID().c_str());
    scrollAndWait(startupMsg);
    snprintf(startupMsg, BUF_SIZE, "IP: %s", WiFi.localIP().toString().c_str());
    scrollAndWait(startupMsg);
    scrollAndWait(mqttStatus);
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  // Prepare default display messages
  if (LEFTTORIGHT)
  {
    defMSG.toCharArray(buf, defMSG.length() + 1);
    reverseString(buf, disp_MSG, defMSG.length() + 1);
  }
  else
  {
    defMSG.toCharArray(disp_MSG,   defMSG.length()   + 1);
    mainMSG.toCharArray(disp_MSG_2, mainMSG.length() + 1);
  }

  strncpy(convTmp1, disp_MSG,   BUF_SIZE - 1); convTmp1[BUF_SIZE - 1] = '\0';
  strncpy(convTmp2, disp_MSG_2, BUF_SIZE - 1); convTmp2[BUF_SIZE - 1] = '\0';
  utf8AsciiConvert(convTmp1, disp_MSG);
  utf8AsciiConvert(convTmp2, disp_MSG_2);

  prevTimeAnim = millis();
  Serial.println("Setup done");
}


// ============================================================
//  loop()
// ============================================================
void loop()
{
  // ----------------------------------------------------------
  //  Portal mode: drive WiFiManager until user connects
  // ----------------------------------------------------------
  if (wifiPortalActive)
  {
    wifiManager.process();  // keeps DNS + HTTP server alive

    // Cycle display pages every WIFI_PAGE_INTERVAL ms
    if (millis() - wifiPageTimer >= WIFI_PAGE_INTERVAL)
    {
      wifiPageTimer = millis();
      wifiPageIndex = (wifiPageIndex + 1) % 3;
      showWifiPage(wifiPageIndex);
    }

    // User saved credentials and WiFi connected
    if (WiFi.status() == WL_CONNECTED)
    {
      wifiPortalActive = false;
      Serial.println("WiFi connected via portal — IP: " + WiFi.localIP().toString());
      mx.displayReset();
      OTA_setup();

      // Run the post-connect startup sequence that setup() skipped
      char startupMsg[BUF_SIZE];
      char mqttStatus[BUF_SIZE];

      if (client.connect("IC-Ticker"))
      {
        client.publish("devlol/test", "IC-Ticker");
        client.publish("devlol/test", WiFi.localIP().toString().c_str());
        client.subscribe("devlol/IoTlights/color");
        client.subscribe("devlol/IC-Ticker");
        snprintf(mqttStatus, BUF_SIZE, "MQTT: OK %s", server.toString().c_str());
      }
      else
      {
        snprintf(mqttStatus, BUF_SIZE, "MQTT FAIL rc=%d", client.state());
      }

      for (int repeat = 0; repeat < 2; repeat++)
      {
        snprintf(startupMsg, BUF_SIZE, "WiFi: %s", WiFi.SSID().c_str());
        scrollAndWait(startupMsg);
        snprintf(startupMsg, BUF_SIZE, "IP: %s", WiFi.localIP().toString().c_str());
        scrollAndWait(startupMsg);
        scrollAndWait(mqttStatus);
      }

      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      printLocalTime();

      if (LEFTTORIGHT)
      {
        defMSG.toCharArray(buf, defMSG.length() + 1);
        reverseString(buf, disp_MSG, defMSG.length() + 1);
      }
      else
      {
        defMSG.toCharArray(disp_MSG,    defMSG.length()   + 1);
        mainMSG.toCharArray(disp_MSG_2, mainMSG.length()  + 1);
      }
      strncpy(convTmp1, disp_MSG,   BUF_SIZE - 1); convTmp1[BUF_SIZE - 1] = '\0';
      strncpy(convTmp2, disp_MSG_2, BUF_SIZE - 1); convTmp2[BUF_SIZE - 1] = '\0';
      utf8AsciiConvert(convTmp1, disp_MSG);
      utf8AsciiConvert(convTmp2, disp_MSG_2);
      prevTimeAnim = millis();
    }

    // Portal timed out without a connection → restart
    if (!wifiManager.getConfigPortalActive() && WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Portal timed out — restarting");
      ESP.restart();
    }

    return;  // skip normal loop while portal is open
  }

  // ----------------------------------------------------------
  //  Normal operation
  // ----------------------------------------------------------

  // OTA handle (only needed when NOT using FreeRTOS task)
#if !(defined ESP32_RTOS && defined ESP32)
  if (otaInitialized) ArduinoOTA.handle();
#endif

  // MQTT
  if (!client.connected()) reconnect();
  client.loop();

  // FastLED
  fill_solid(leds, NUMPIXELS, color);
  FastLED.show();

  // Buttons
  if (!digitalRead(BUTTON_BLUE)) { Serial.println("Pressed BLUE"); color = CRGB(0,   0, 200); }
  if (!digitalRead(BUTTON_RED))  { Serial.println("Pressed RED");  color = CRGB(200, 0,   0); }

  // Time display — refresh every 60 s
  if (millis() - previouscall > 60000 && mx.displayAnimate())
  {
    printLocalTime();
    mx.displayReset();
    if (change)
    {
      mx.setSpeed(15);
      mx.setPause(1000);
      mx.displayText(timeMSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
    else
    {
      mx.setSpeed(25);
      mx.setPause(10);
      mx.displayText(dateMSG, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    }
    change = !change;
    previouscall = millis();
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
  }

  // Message display
  if (mx.displayAnimate())
  {
    mx.displayReset();
    if (newMessageAvailable)
    {
      mx.setSpeed(25);
      mx.setPause(10);
      mx.displayText(curMessage, PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      if (++showAgain >= 5)
      {
        showAgain = 0;
        newMessageAvailable = false;
      }
    }
    else
    {
      mx.setSpeed(15);
      mx.setPause(1000);
      mx.displayText(
        flip ? disp_MSG_2 : disp_MSG,
        PA_CENTER, mx.getSpeed(), mx.getPause(), PA_SCROLL_LEFT, PA_SCROLL_LEFT
      );
      flip = !flip;
    }
  }
}