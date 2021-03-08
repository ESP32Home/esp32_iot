#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <MQTT.h>

#include <ConfigUtils.h>
#include <EspNowWrapper.h>
#include <ESPStringUtils.h>

DynamicJsonDocument config(5*1024);//5 KB
DynamicJsonDocument secret(1*1024);//1 KB
MQTTClient mqtt(1*1024);
WiFiClient wifi;//needed to stay on global scope
NowApp espnow;

uint32_t cycle_count = 0;

//https://esp32.com/viewtopic.php?t=12992

void meshMessage(String &payload,String from){
  Serial.printf("RX> from(%s) => [%s]\n",from.c_str(),payload.c_str());
  mqtt.publish("espnow/"+from,payload);
}

void setup() {
  
  Serial.begin(115200);
  timelog("Boot ready");

  load_json(config,"/config.json");
  load_json(secret,"/secret.json");
  timelog("config loaded");

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  timelog("wifi mode set");

  espnow.start(config,secret);
  espnow.onMessage(meshMessage);

  //espnow.set_channel(config["wifi"]["channel"]);
  timelog("setup() done");
  String msg("Good Morning Everyone (0)");
  espnow.broadcast(msg);
  timelog("initial broadcast() done");

}

void loop() {
  cycle_count++;
  Serial.printf("\n\n");
  timelog("loop start cycle ("+String(cycle_count)+")");
  espnow.print_info();
  espnow.broadcast("Hello Everyone ("+String(cycle_count)+")");
  delay(5000);
}
