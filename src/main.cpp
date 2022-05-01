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
#include "Parola_Fonts_data.h"

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
    delay(3500);
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

WiFiManager wifiManager;
MD_Parola mx = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES); // SPI hardware interface
// MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel smile(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define ANIMATION_DELAY 150
#define MAX_FRAMES 4
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

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// String mainMSG = "Dreißigster April Zweitausenundzweiundzwanzig";
String mainMSG = "The tram is near";
String noWifiMSG = "NO WIFI visit ... AP: Q-Ticker ... PW: qujochoe ... IP: 192.168.4.1";
String defMSG = "qujOchÖ Ticker";

const char *timezone = "CET-1CEST,M3.5.0/02,M10.5.0/03"; // = CET/CEST  --> for adjusting your local time zone see: https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
const char *ntpServer = "europe.pool.ntp.org";           // pool.ntp.org       // server pool prefix "2." could be necessary with IPv6

struct tm tm;

char disp_MSG[BUF_SIZE];
char disp_MSG_2[BUF_SIZE];
char mqttMSG[BUF_SIZE];
char buf[BUF_SIZE];
char timeMSG[25];
char dateMSG[25];

char curMessage[BUF_SIZE] = {""};
char newMessage[BUF_SIZE] = {"Hello! Enter new message?"};

unsigned long previouscall = 0;
int showAgain = 0;
bool newMessageAvailable = true;
bool change;
bool flip;

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
    mx.setFont(ExtASCII);
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
    mainMSG.toCharArray(disp_MSG_2, mainMSG.length() + 1);
  }

  utf8AsciiConvert(disp_MSG, disp_MSG);
  utf8AsciiConvert(disp_MSG_2, disp_MSG_2);

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
      client.subscribe("devlol/Q-Ticker");
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
  else if (strcmp(topic, "devlol/Q-Ticker") == 0)
  {
    String mqttBuff = "";
    if (length > 99)
      length = 99;

    for (int i = 0; i < length; i++)
    {
      mqttBuff = mqttBuff + (char)payload[i];
    }

    mqttBuff.toCharArray(curMessage, mqttBuff.length() + 1);
    utf8AsciiConvert(curMessage, curMessage);
    newMessageAvailable = true;
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
  strftime(dateMSG, sizeof(timeinfo), "%a, %B %d %Y", &timeinfo);
  strftime(timeMSG, sizeof(timeinfo), "%H:%M:%S", &timeinfo);
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

void utf8AsciiConvert(char *src, char *des) // converts array form source array "src" to destination array "des"
{
  int k = 0;
  char c;
  for (int i = 0; src[i]; i++)
  {
    c = utf8Ascii(src[i]);
    if (c != '\0') // if (c!=0)
      des[k++] = c;
    else
    {
      des[k] = '\0'; // des[k]=0;
    }
  }
  des[k] = '\0'; // des[k]=0;
}

uint8_t utf8Ascii(uint8_t ascii)
// Convert a single Character from UTF8 to Extended ASCII according to ISO 8859-1,
// also called ISO Latin-1. Codes 128-159 contain the Microsoft Windows Latin-1
// extended characters:
// - codes 0..127 are identical in ASCII and UTF-8
// - codes 160..191 in ISO-8859-1 and Windows-1252 are two-byte characters in UTF-8
//                 + 0xC2 then second byte identical to the extended ASCII code.
// - codes 192..255 in ISO-8859-1 and Windows-1252 are two-byte characters in UTF-8
//                 + 0xC3 then second byte differs only in the first two bits to extended ASCII code.
// - codes 128..159 in Windows-1252 are different, but usually only the €-symbol will be needed from this range.
//                 + The euro symbol is 0x80 in Windows-1252, 0xa4 in ISO-8859-15, and 0xe2 0x82 0xac in UTF-8.
//
// Modified from original code at http://playground.arduino.cc/Main/Utf8ascii
// Extended ASCII encoding should match the characters at http://www.ascii-code.com/
//
// Return "0" if a byte has to be ignored.
{
  static uint8_t cPrev;
  uint8_t c = '\0';

  PRINTX("\nutf8Ascii 0x", ascii);

  if (ascii < 0x7f) // Standard ASCII-set 0..0x7F, no conversion
  {
    cPrev = '\0';
    c = ascii;
  }
  else
  {
    switch (cPrev) // Conversion depending on preceding UTF8-character
    {
    case 0xC2:
      c = ascii;
      break;
    case 0xC3:
      c = ascii | 0xC0;
      break;
    case 0x82:
      if (ascii == 0xAC)
        c = 0x80; // Euro symbol special case
    case 0xE2:
      switch (ascii)
      {
      case 0x80:
        c = 133;
        break; // ellipsis special case
      }
      break;

    default:
      PRINTS("!Unhandled! ");
    }
    cPrev = ascii; // save last char
  }

  PRINTX(" -> 0x", c);

  return (c);
}

void utf8Ascii(char *s)
// In place conversion UTF-8 string to Extended ASCII
// The extended ASCII string is always shorter.
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
  *cp = '\0'; // terminate the new string
}
