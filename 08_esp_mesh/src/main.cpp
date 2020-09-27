#include <Arduino.h>
#include <ArduinoJson.h>

#include <ConfigUtils.h>
#include <EspMeshWrapper.h>

DynamicJsonDocument config(5*1024);//5 KB
DynamicJsonDocument secret(1*1024);//1 KB
MeshApp mesh;

uint32_t cycle_count = 0;

void meshMessage(String &payload,String from,int flag){
  Serial.printf("RX> from(%s) => [%s]\n",from.c_str(),payload.c_str());
}

void setup() {
  
  Serial.begin(115200);
  timelog("Boot ready");

  load_json(config,"/config.json");
  load_json(secret,"/secret.json");
  timelog("config loaded");

  mesh.start(config,secret);
  mesh.onMessage(meshMessage);


  timelog("setup() done");

}

void loop() {
  cycle_count++;
  Serial.printf("\n\n");
  timelog("loop start cycle ("+String(cycle_count)+")");
  mesh.print_info();
  if(mesh.connected()){
    mesh.send_down("Hello dowlink neighbors ("+String(cycle_count)+")");
    delay(5000);
    if(!mesh.is_root()){
      mesh.send_parent("Hello parent ("+String(cycle_count)+")");
      delay(5000);
    }
    //String host("10.0.0.31:867");
    ////String host("10.0.0.27:867");
    //String message("Hello Out World ("+String(cycle_count)+")\n");
    //mesh.send_out(host,message);
    //delay(5000);
  }else{
    Serial.printf("Mesh not connected yet...\n");
    delay(5000);
  }
}
