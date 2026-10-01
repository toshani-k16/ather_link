#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define ENABLE_LORA 0
#if ENABLE_LORA
#include <SPI.h>
#include <LoRa.h>
#endif

const char* WIFI_SSID="OnePlus Nord CE4";
const char* WIFI_PASSWORD="12341234";
const char* SERVER_URL="http://10.143.240.171:5000/api/endpoint";

const bool IS_RECEIVER_GATEWAY=true;
uint32_t RECEIVER_NODE_ID=730528681;

#define MESH_PREFIX "AetherMesh"
#define MESH_PASSWORD "AetherMeshPassword"
#define MESH_PORT 5555
#define MESH_CHANNEL 6

#define BUTTON_PIN 4
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

const double FALLBACK_LAT=12.9716;
const double FALLBACK_LNG=77.5946;

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_REQUESTED_MTU 247
#define BLE_CHUNK_SIZE 180
#define DEDUP_BUFFER_SIZE 24
#define LORA_MAX_TTL 8

#if ENABLE_LORA
#define LORA_SCK_PIN 18
#define LORA_MISO_PIN 19
#define LORA_MOSI_PIN 23
#define LORA_SS_PIN 5
#define LORA_RST_PIN 14
#define LORA_DIO0_PIN 2
#define LORA_FREQUENCY 433E6
#endif

painlessMesh mesh;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
BLEServer *pServer=nullptr;
BLECharacteristic *pCharacteristic=nullptr;
bool deviceConnected=false,lastButtonState=HIGH;
unsigned long lastDebounceTime=0;
const unsigned long debounceDelay=300;
String recentPacketIds[DEDUP_BUFFER_SIZE];
int dedupIndex=0;

void connectToWiFi();
void sendToFlaskServer(const String&);
void sendSOS();
void routeBluetoothJSON(String);
void receivedCallback(uint32_t,String&);
void handlePacketAtDestination(JsonDocument&);
void bleSendJSON(const String&);
void ensurePacketId(JsonDocument&);

#if ENABLE_LORA
void loraSendJSON(const String&);
void loraPoll();
#endif

bool isDuplicatePacket(const String &id){
  if(!id.length())return false;
  for(int i=0;i<DEDUP_BUFFER_SIZE;i++)if(recentPacketIds[i]==id)return true;
  recentPacketIds[dedupIndex]=id;
  dedupIndex=(dedupIndex+1)%DEDUP_BUFFER_SIZE;
  return false;
}

void ensurePacketId(JsonDocument &doc){
  if(!doc["packet_id"].is<const char*>())
    doc["packet_id"]="PKT-"+String(random(100000,999999));
}

void connectToWiFi(){
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  unsigned long t=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-t<15000)delay(500);
}

void sendToFlaskServer(const String &json){
  if(WiFi.status()!=WL_CONNECTED)return;
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type","application/json");
  int code=http.POST(json);
  Serial.printf("[FLASK] HTTP Response: %d\n",code);
  if(code>0)Serial.println(http.getString());
  else Serial.println(http.errorToString(code));
  http.end();
}

class BLECallbacks:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic *c)override{
    String rx=c->getValue().c_str();
    if(rx.length()){
      Serial.println("[BLE] JSON received:");
      Serial.println(rx);
      routeBluetoothJSON(rx);
    }
  }
};

class ServerCallbacks:public BLEServerCallbacks{
  void onConnect(BLEServer*)override{
    deviceConnected=true;
    Serial.println("[BLE] Smartphone connected");
  }
  void onDisconnect(BLEServer *s)override{
    deviceConnected=false;
    Serial.println("[BLE] Smartphone disconnected");
    s->getAdvertising()->start();
  }
};

void routeBluetoothJSON(String json){
  JsonDocument doc;
  if(deserializeJson(doc,json)){
    Serial.println("[BLE ERROR] Invalid JSON");
    return;
  }

  ensurePacketId(doc);
  doc["source_type"]="SMARTPHONE_BLE";
  doc["gateway_node"]=mesh.getNodeId();
  doc["ttl"]=doc["ttl"]|LORA_MAX_TTL;

  uint32_t target=doc["target_node"]|RECEIVER_NODE_ID;
  doc["target_node"]=target;

  JsonArray trace=doc["route_trace"];
  if(trace.isNull())trace=doc["route_trace"].to<JsonArray>();
  trace.add(mesh.getNodeId());

  if(target==mesh.getNodeId()){
    handlePacketAtDestination(doc);
    return;
  }

  String payload;
  serializeJson(doc,payload);

  if(mesh.sendSingle(target,payload))
    Serial.printf("[MESH] Phone JSON -> %u\n",target);
  else{
#if ENABLE_LORA
    loraSendJSON(payload);
#else
    Serial.println("[MESH ERROR] Target route unavailable");
#endif
  }
}

void sendSOS(){
  JsonDocument doc;
  doc["packet_id"]="SOS-"+String(random(100000,999999));
  doc["source_type"]="HANDHELD_BUTTON";
  doc["source_node"]=mesh.getNodeId();
  doc["target_node"]=RECEIVER_NODE_ID;
  doc["ttl"]=LORA_MAX_TTL;

  JsonObject data=doc["data"].to<JsonObject>();
  data["category"]="SOS";
  data["item"]="Panic Button Triggered";
  data["priority"]="CRITICAL";

  JsonObject g=doc["gps"].to<JsonObject>();
  if(gps.location.isValid()){
    g["lat"]=gps.location.lat();
    g["lng"]=gps.location.lng();
    g["accuracy"]="GPS_LOCK";
  }else{
    g["lat"]=FALLBACK_LAT;
    g["lng"]=FALLBACK_LNG;
    g["accuracy"]="STATIC_FALLBACK";
  }

  JsonArray trace=doc["route_trace"].to<JsonArray>();
  trace.add(mesh.getNodeId());

  String payload;
  serializeJson(doc,payload);

  if(IS_RECEIVER_GATEWAY){
    handlePacketAtDestination(doc);
    return;
  }

  if(mesh.sendSingle(RECEIVER_NODE_ID,payload))
    Serial.printf("[MESH] SOS -> %u\n",RECEIVER_NODE_ID);
  else{
#if ENABLE_LORA
    loraSendJSON(payload);
#else
    Serial.println("[MESH ERROR] Gateway route unavailable");
#endif
  }
}

void receivedCallback(uint32_t from,String &msg){
  JsonDocument doc;
  if(deserializeJson(doc,msg))return;

  String id=doc["packet_id"]|"";
  if(isDuplicatePacket(id))return;

  uint32_t me=mesh.getNodeId();
  uint32_t target=doc["target_node"]|0;

  JsonArray trace=doc["route_trace"];
  if(!trace.isNull())trace.add(me);

  if(target==me||IS_RECEIVER_GATEWAY){
    handlePacketAtDestination(doc);
    return;
  }

  String payload;
  serializeJson(doc,payload);

  if(mesh.sendSingle(target,payload))
    Serial.printf("[RELAY] %u -> %u\n",from,target);
  else
    Serial.printf("[RELAY ERROR] -> %u\n",target);
}

void handlePacketAtDestination(JsonDocument &doc){
  Serial.println("\n========== [TARGET REACHED] ==========");
  serializeJsonPretty(doc,Serial);
  Serial.println("\n======================================");

  if(IS_RECEIVER_GATEWAY){
    String payload;
    serializeJson(doc,payload);
    sendToFlaskServer(payload);
  }

  if(deviceConnected){
    String output;
    serializeJson(doc,output);
    bleSendJSON(output);
  }
}

void bleSendJSON(const String &payload){
  if(!deviceConnected)return;

  int total=payload.length();
  int chunks=(total+BLE_CHUNK_SIZE-1)/BLE_CHUNK_SIZE;
  if(chunks==0)chunks=1;

  for(int i=0;i<chunks;i++){
    int start=i*BLE_CHUNK_SIZE;
    int end=min(total,start+BLE_CHUNK_SIZE);
    String chunk=String(i+1)+"/"+String(chunks)+":"+payload.substring(start,end);
    pCharacteristic->setValue(chunk.c_str());
    pCharacteristic->notify();
    delay(20);
  }
}

#if ENABLE_LORA
void loraSendJSON(const String &payload){
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
}

void loraPoll(){
  int size=LoRa.parsePacket();
  if(!size)return;

  String received;
  while(LoRa.available())received+=(char)LoRa.read();

  JsonDocument doc;
  if(deserializeJson(doc,received))return;

  String id=doc["packet_id"]|"";
  if(isDuplicatePacket(id))return;

  uint32_t target=doc["target_node"]|0;
  uint32_t me=mesh.getNodeId();
  int ttl=doc["ttl"]|LORA_MAX_TTL;

  if(target==me||IS_RECEIVER_GATEWAY){
    handlePacketAtDestination(doc);
    return;
  }

  if(--ttl<=0)return;
  doc["ttl"]=ttl;

  JsonArray trace=doc["route_trace"];
  if(!trace.isNull())trace.add(me);

  String payload;
  serializeJson(doc,payload);
  mesh.sendSingle(target,payload);
  loraSendJSON(payload);
}
#endif

void setup(){
  Serial.begin(115200);
  randomSeed(esp_random());

  pinMode(BUTTON_PIN,INPUT_PULLUP);
  gpsSerial.begin(9600,SERIAL_8N1,GPS_RX_PIN,GPS_TX_PIN);

  connectToWiFi();

  mesh.setDebugMsgTypes(ERROR|STARTUP);
  mesh.init(MESH_PREFIX,MESH_PASSWORD,MESH_PORT,WIFI_AP_STA,MESH_CHANNEL);
  mesh.onReceive(&receivedCallback);

  if(IS_RECEIVER_GATEWAY){
    mesh.setRoot(true);
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] CENTRAL OFFICE | ID: %u\n",mesh.getNodeId());
  }else{
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] HANDHELD/RELAY | ID: %u\n",mesh.getNodeId());
  }

  BLEDevice::init("AetherNode_ESP32");
  BLEDevice::setMTU(BLE_REQUESTED_MTU);

  pServer=BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *service=pServer->createService(SERVICE_UUID);
  pCharacteristic=service->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ|
    BLECharacteristic::PROPERTY_WRITE|
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->setCallbacks(new BLECallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising *adv=BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

#if ENABLE_LORA
  SPI.begin(LORA_SCK_PIN,LORA_MISO_PIN,LORA_MOSI_PIN,LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN,LORA_RST_PIN,LORA_DIO0_PIN);
  if(LoRa.begin(LORA_FREQUENCY))Serial.println("[SYSTEM] LoRa initialized");
  else Serial.println("[LORA ERROR] Init failed");
#endif

  Serial.println("\n========== AETHERLINK READY ==========");
  Serial.printf("Node ID: %u\n",mesh.getNodeId());
  Serial.println("======================================");
}

void loop(){
  mesh.update();

  while(gpsSerial.available()>0)
    gps.encode(gpsSerial.read());

#if ENABLE_LORA
  loraPoll();
#endif

  int reading=digitalRead(BUTTON_PIN);

  if(reading==LOW&&lastButtonState==HIGH&&
     millis()-lastDebounceTime>debounceDelay){
    lastDebounceTime=millis();
    Serial.println("[ALERT] HARDWARE SOS");
    sendSOS();
  }

  lastButtonState=reading;
}
