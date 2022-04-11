#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager       version = 2.0.3-alpha
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#define ESP32_RTOS

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW 
#define MAX_DEVICES 12
#define CLK_PIN   18  // or SCK
#define DATA_PIN  23  // or MOSI
#define CS_PIN    5  // or SS

#define LED_PIN 27
#define NUMPIXELS 8
#define BUTTON_RED 4
#define BUTTON_BLUE 14

IPAddress server(158, 255, 212, 248);

#define PRINT(s, v)   // Print a string followed by a value (decimal)
#define PRINTX(s, v)  // Print a string followed by a value (hex)
#define PRINTB(s, v)  // Print a string followed by a value (binary)
#define PRINTC(s, v)  // Print a string followed by a value (char)
#define PRINTS(s)     // Print a string


#if defined(ESP32_RTOS) && defined(ESP32)
void ota_handle( void * parameter ) {
  for (;;) {
    ArduinoOTA.handle();
    delay(3500);
  }
}
#endif

void setupOTA_Wifi(const char* nameprefix, const char* portalpw);
void reconnect();

WiFiManager wifiManager;
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);                      // SPI hardware interface
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel smile(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// --------------------
// Constant parameters
//
#define ANIMATION_DELAY 75	// milliseconds
#define MAX_FRAMES      4   // number of animation frames

// ========== General Variables ===========
//
const uint8_t pacman[MAX_FRAMES][18] =  // ghost pursued by a pacman
{
  { 0xfe, 0x73, 0xfb, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0x7e, 0xff, 0xe7, 0xc3, 0x81, 0x00 },
  { 0xfe, 0x7b, 0xf3, 0x7f, 0xfb, 0x73, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xe7, 0xe7, 0x42, 0x00 },
  { 0xfe, 0x73, 0xfb, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xff, 0xe7, 0x66, 0x24 },
  { 0xfe, 0x7b, 0xf3, 0x7f, 0xf3, 0x7b, 0xfe, 0x00, 0x00, 0x00, 0x3c, 0x7e, 0xff, 0xff, 0xff, 0xff, 0x7e, 0x3c },
};
const uint8_t DATA_WIDTH = (sizeof(pacman[0])/sizeof(pacman[0][0]));

uint32_t prevTimeAnim = 0;  // remember the millis() value in animations
int16_t idx;                // display index (column)
uint8_t frame;              // current animation frame
uint8_t deltaFrame;         // the animation frame offset for the next frame

uint32_t color;
int r = 0;
int g = 0;
int b = 0;

// ========== Control routines ===========
//
void resetMatrix(void)
{
  mx.control(MD_MAX72XX::INTENSITY, MAX_INTENSITY/2);
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
  mx.clear();
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


void setup()
{
  Serial.begin(115200);
  client.setServer(server, 1883);
  client.setCallback(callback);

  setupOTA_Wifi("IC-Ticker", "IC-42022");    // Name-xxxx.local PW for Portal

  mx.begin();
  resetMatrix();
  prevTimeAnim = millis();

  smile.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  smile.show();            // Turn OFF all pixels ASAP
  smile.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)
}

void loop()
{
  #ifdef defined(ESP32_RTOS) && defined(ESP32)
  #else // If you do not use FreeRTOS, you have to regulary call the handle method.
    ArduinoOTA.handle();
  #endif

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  for (int i = 0; i < NUMPIXELS; i++)
  {
    smile.setPixelColor(i, smile.Color(255,   100,   100));
  }
  smile.show();

  static boolean bInit = true;  // initialise the animation

  // Is it time to animate?
  if (millis()-prevTimeAnim < ANIMATION_DELAY)
    return;
  prevTimeAnim = millis();      // starting point for next time

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  // Initialize
  if (bInit)
  {
    mx.clear();
    idx = -DATA_WIDTH;
    frame = 0;
    deltaFrame = 1;
    bInit = false;

    // Lay out the dots
    for (uint8_t i=0; i<MAX_DEVICES; i++)
    {
      mx.setPoint(3, (i*COL_SIZE) + 3, true);
      mx.setPoint(4, (i*COL_SIZE) + 3, true);
      mx.setPoint(3, (i*COL_SIZE) + 4, true);
      mx.setPoint(4, (i*COL_SIZE) + 4, true);
    }
  }

  // clear old graphic
  for (uint8_t i=0; i<DATA_WIDTH; i++)
    mx.setColumn(idx-DATA_WIDTH+i, 0);
  // move reference column and draw new graphic
  idx++;
  for (uint8_t i=0; i<DATA_WIDTH; i++)
    mx.setColumn(idx-DATA_WIDTH+i, pacman[frame][i]);

  // advance the animation frame
  frame += deltaFrame;
  if (frame == 0 || frame == MAX_FRAMES-1)
    deltaFrame = -deltaFrame;

  // check if we are completed and set initialise for next time around
  bInit = (idx == mx.getColumnCount()+DATA_WIDTH);

  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);

  return;
}

void setupOTA_Wifi(const char* nameprefix, const char* portalpw) 
{
  // Configure the hostname
  uint16_t maxlen = strlen(nameprefix) + 7;
  char *fullhostname = new char[maxlen];
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(fullhostname, maxlen, "%s-%02x%02x%02x", nameprefix, mac[3], mac[4], mac[5]);
  
  //wifiManager.resetSettings();
  //SPIFFS.format();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(fullhostname);
  
  if (!wifiManager.autoConnect(nameprefix, portalpw))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
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

  ArduinoOTA.onStart([]() {
	//NOTE: make .detach() here for all functions called by Ticker.h library - not to interrupt transfer process in any way.
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("\nAuth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("\nBegin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("\nConnect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("\nReceive Failed");
    else if (error == OTA_END_ERROR) Serial.println("\nEnd Failed");
  });

  ArduinoOTA.begin();

  Serial.println("OTA Initialized");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

#if defined(ESP32_RTOS) && defined(ESP32)
  xTaskCreate(
    ota_handle,          /* Task function. */
    "OTA_HANDLE",        /* String with name of task. */
    10000,            /* Stack size in bytes. */
    NULL,             /* Parameter passed as input of the task */
    1,                /* Priority of the task. */
    NULL);            /* Task handle. */
#endif
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("arduinoClient")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("devlol/IoTlights", "IC-Ticker");
      // ... and resubscribe
      client.subscribe("devlol/IoTlights/color");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}