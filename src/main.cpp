#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>    // https://github.com/tzapu/WiFiManager       version = 2.0.3-alpha
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define ESP32_RTOS

char PortalName[20];
uint32_t chipId = 0;

#if defined(ESP32_RTOS) && defined(ESP32)
void ota_handle( void * parameter ) {
  for (;;) {
    ArduinoOTA.handle();
    delay(3500);
  }
}
#endif

void setupOTA_Wifi(const char* nameprefix);


WiFiManager wifiManager;


void setup()
{
  Serial.begin(115200);
  setupOTA_Wifi("IC-Tracker");    // Name-xxxx.local
}

void loop()
{
  #ifdef defined(ESP32_RTOS) && defined(ESP32)
  #else // If you do not use FreeRTOS, you have to regulary call the handle method.
    ArduinoOTA.handle();
  #endif
}

void setupOTA_Wifi(const char* nameprefix) 
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

  // unique ESP Id
  // for(int i=0; i<17; i=i+8)
  // {
	//   chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
	// }
  // snprintf(PortalName, sizeof(PortalName), "ESP_%d", chipId);

  WiFi.setHostname(fullhostname);
  
  delete[] fullhostname;

  Serial.println(PortalName);

  if (!wifiManager.autoConnect(PortalName, "EnterThis"))
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