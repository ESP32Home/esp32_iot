#include <Arduino.h>
#include <ArduinoJson.h>

#include <ConfigUtils.h>
#include <EspNowWrapper.h>

DynamicJsonDocument config(5*1024);//5 KB
DynamicJsonDocument secret(1*1024);//1 KB
NowApp espnow;

uint32_t cycle_count = 0;

void meshMessage(String &payload,String from){
  Serial.printf("RX> from(%s) => [%s]\n",from.c_str(),payload.c_str());
}

void setup() {
  
  Serial.begin(115200);
  timelog("Boot ready");

  load_json(config,"/config.json");
  load_json(secret,"/secret.json");
  timelog("config loaded");

  espnow.start(config,secret);
  espnow.onMessage(meshMessage);

  timelog("setup() done");

}

void loop() {
  cycle_count++;
  Serial.printf("\n\n");
  timelog("loop start cycle ("+String(cycle_count)+")");
  espnow.print_info();
  espnow.broadcast("Hello Everyone ("+String(cycle_count)+")");
  delay(5000);
}
