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

String topic;

void meshMessage(String &payload,String from){
  Serial.printf("RX> from(%s) => [%s]\n",from.c_str(),payload.c_str());
  mqtt.publish("espnow/"+from,payload);
}

void mqtt_start(DynamicJsonDocument &config){
  mqtt.begin(config["mqtt"]["host"],config["mqtt"]["port"], wifi);
  if(mqtt.connect(config["mqtt"]["client_id"])){
    Serial.println("mqtt>connected");
  }
}


void setup() {
  
  Serial.begin(115200);
  timelog("Boot ready");

  load_json(config,"/config.json");
  load_json(secret,"/secret.json");
  timelog("config loaded");

  String ssid = secret["wifi"]["access_point"];
  String pass = secret["wifi"]["password"];
  WiFi.begin(ssid.c_str(),pass.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  timelog("config loaded");


  byte mac[6];
  WiFi.macAddress(mac);
  topic = "espnow/"+hextab_to_string(mac);

  espnow.start(config,secret);
  espnow.onMessage(meshMessage);

  mqtt_start(config);
  mqtt.publish(topic,"gateway (start)");
  mqtt.loop();
  timelog("setup() done");

}

void loop() {
  cycle_count++;
  Serial.printf("\n\n");
  timelog("loop start cycle ("+String(cycle_count)+")");
  mqtt.publish(topic,"gateway("+String(cycle_count)+")");
  Serial.print(WiFi.channel());
  delay(5000);
}
