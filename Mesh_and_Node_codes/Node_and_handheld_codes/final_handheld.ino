#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define ENABLE_LORA 0
#if ENABLE_LORA
  #include <SPI.h>
  #include <LoRa.h>
#endif

const bool IS_RECEIVER_GATEWAY = false;
uint32_t RECEIVER_NODE_ID = 730528681;

#define MESH_PREFIX        "AetherMesh"
#define MESH_PASSWORD      "AetherMeshPassword"
#define MESH_PORT          5555
#define MESH_CHANNEL       6

#define BUTTON_PIN         4        
#define LED_PIN            2        
#define GPS_RX_PIN         16       
#define GPS_TX_PIN         17       

#define LORA_SCK_PIN       18
#define LORA_MISO_PIN      19
#define LORA_MOSI_PIN      23
#define LORA_SS_PIN        5
#define LORA_RST_PIN       14
#define LORA_DIO0_PIN      2
#define LORA_FREQUENCY     433E6
#define LORA_MAX_TTL       8

const double FALLBACK_LAT = 12.9716;
const double FALLBACK_LNG = 77.5946;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_REQUESTED_MTU   247
#define BLE_CHUNK_SIZE      180

painlessMesh mesh;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 300;
bool lastButtonState = HIGH;

#define DEDUP_BUFFER_SIZE 24
String recentPacketIds[DEDUP_BUFFER_SIZE];
int dedupIndex = 0;

bool isDuplicatePacket(const String &packetId) {
  if (packetId.length() == 0) return false;
  for (int i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    if (recentPacketIds[i] == packetId) return true;
  }
  recentPacketIds[dedupIndex] = packetId;
  dedupIndex = (dedupIndex + 1) % DEDUP_BUFFER_SIZE;
  return false;
}

void sendSOS();
void routeBluetoothJSON(String jsonPayloadStr);
void receivedCallback(uint32_t from, String &msg);
void handlePacketAtDestination(JsonDocument &doc);
void bleSendJSON(const String &payload);
void ensurePacketId(JsonDocument &doc);
void blinkLED(int times, int delayMs); // [NEW] Declaration
#if ENABLE_LORA
void loraSendJSON(const String &payload);
void loraPoll();
#endif

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

class BLECallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0) {
            Serial.println("\n[BLE] Received JSON payload from smartphone:");
            Serial.println(rxValue);
            routeBluetoothJSON(rxValue);
        }
    }
};

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      Serial.println("[BLE] Smartphone connected!");
    }
    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      Serial.println("[BLE] Smartphone disconnected. Restarting advertising...");
      pServer->getAdvertising()->start();
    }
};

void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);         
  digitalWrite(LED_PIN, LOW);       
  
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT, WIFI_AP_STA, MESH_CHANNEL);
  mesh.onReceive(&receivedCallback);

  if (IS_RECEIVER_GATEWAY) {
    mesh.setRoot(true);
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] CENTRAL RECEIVER GATEWAY. Node ID: %u\n", mesh.getNodeId());
  } else {
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] HANDHELD / RELAY NODE. Node ID: %u\n", mesh.getNodeId());
  }

  BLEDevice::init("AetherNode_ESP32");
  BLEDevice::setMTU(BLE_REQUESTED_MTU);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new BLECallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("[SYSTEM] BLE GATT Server active. Waiting for mobile app pairing...");

#if ENABLE_LORA
  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_SS_PIN);
  LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LORA ERROR] Radio init failed. Check wiring/frequency.");
  } else {
    Serial.println("[SYSTEM] LoRa radio initialized.");
  }
#endif
}

void loop() {
  mesh.update();

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

#if ENABLE_LORA
  loraPoll();
#endif

  int reading = digitalRead(BUTTON_PIN);
  if (reading == LOW && lastButtonState == HIGH) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      Serial.println("\n[ALERT] HARDWARE SOS BUTTON PRESSED!");
      sendSOS();
      lastDebounceTime = millis();
    }
  }
  lastButtonState = reading;
}

void ensurePacketId(JsonDocument &doc) {
  if (!doc["packet_id"].is<const char*>()) {
    doc["packet_id"] = "PKT-" + String(random(100000, 999999));
  }
}

void routeBluetoothJSON(String jsonPayloadStr) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonPayloadStr);

  if (err) return;

  ensurePacketId(doc);
  doc["source_type"] = "SMARTPHONE_BLE";
  doc["gateway_node"] = mesh.getNodeId();
  doc["ttl"] = doc["ttl"] | LORA_MAX_TTL;

  uint32_t target = doc["target_node"] | RECEIVER_NODE_ID;
  doc["target_node"] = target;

  JsonArray routeTrace = doc["route_trace"];
  if (routeTrace.isNull()) {
    routeTrace = doc["route_trace"].to<JsonArray>();
  }
  routeTrace.add(mesh.getNodeId());

  if (IS_RECEIVER_GATEWAY || target == mesh.getNodeId()) {
    handlePacketAtDestination(doc);
    return;
  }

  String outputPayload;
  serializeJson(doc, outputPayload);

  if (mesh.sendSingle(target, outputPayload)) {
    Serial.printf("[MESH] Phone BLE packet routed toward Target Node: %u\n", target);
  } else {
#if ENABLE_LORA
    loraSendJSON(outputPayload);
#else
    mesh.sendBroadcast(outputPayload);
#endif
  }
}

void sendSOS() {
  JsonDocument doc;

  doc["packet_id"] = "SOS-" + String(random(100000, 999999));
  doc["source_type"] = "HANDHELD_BUTTON";
  doc["source_node"] = mesh.getNodeId();
  doc["target_node"] = RECEIVER_NODE_ID;
  doc["ttl"] = LORA_MAX_TTL;

  JsonObject data = doc["data"].to<JsonObject>();
  data["category"] = "SOS";
  data["item"] = "Panic Button Triggered";
  data["priority"] = "CRITICAL";

  JsonObject gpsObj = doc["gps"].to<JsonObject>();
  if (gps.location.isValid()) {
    gpsObj["lat"] = gps.location.lat();
    gpsObj["lng"] = gps.location.lng();
    gpsObj["accuracy"] = "GPS_LOCK";
  } else {
    gpsObj["lat"] = FALLBACK_LAT;
    gpsObj["lng"] = FALLBACK_LNG;
    gpsObj["accuracy"] = "STATIC_FALLBACK";
  }

  JsonArray routeTrace = doc["route_trace"].to<JsonArray>();
  routeTrace.add(mesh.getNodeId());

  isDuplicatePacket(doc["packet_id"].as<String>());

  String payload;
  serializeJson(doc, payload);

  blinkLED(3, 100); 
  digitalWrite(LED_PIN, HIGH); 

  if (mesh.sendSingle(RECEIVER_NODE_ID, payload)) {
    Serial.printf("[MESH] Hardware SOS packet routed toward Gateway Node: %u\n", RECEIVER_NODE_ID);
  } else {
#if ENABLE_LORA
    loraSendJSON(payload);
#else
    mesh.sendBroadcast(payload);
#endif
  }
}

void receivedCallback(uint32_t from, String &msg) {
  JsonDocument doc;
  if (deserializeJson(doc, msg)) return;

  String packetId = doc["packet_id"] | "";
  if (isDuplicatePacket(packetId)) return;

  uint32_t myNodeId = mesh.getNodeId();
  uint32_t targetNode = doc["target_node"] | 0;

  JsonArray routeTrace = doc["route_trace"];
  if (!routeTrace.isNull()) {
    routeTrace.add(myNodeId);
  }

  if (IS_RECEIVER_GATEWAY || targetNode == myNodeId) {
    handlePacketAtDestination(doc);
  } else {
    String outputPayload;
    serializeJson(doc, outputPayload);
    mesh.sendSingle(targetNode, outputPayload);
    Serial.printf("[RELAY] Forwarded packet from Node %u to Target %u\n", from, targetNode);
    
    blinkLED(1, 50); 
  }
}

void handlePacketAtDestination(JsonDocument &doc) {
  Serial.println("\n================ [TARGET REACHED] ================");
  serializeJsonPretty(doc, Serial);
  Serial.println("\n==================================================");

  if (deviceConnected) {
    String output;
    serializeJson(doc, output);
    bleSendJSON(output);
  }
}

void bleSendJSON(const String &payload) {
  if (!deviceConnected) return;
  int totalLen = payload.length();
  int totalChunks = (totalLen + BLE_CHUNK_SIZE - 1) / BLE_CHUNK_SIZE;
  if (totalChunks == 0) totalChunks = 1;

  for (int i = 0; i < totalChunks; i++) {
    int start = i * BLE_CHUNK_SIZE;
    int end = min(totalLen, start + BLE_CHUNK_SIZE);
    String chunk = payload.substring(start, end);
    String framed = String(i + 1) + "/" + String(totalChunks) + ":" + chunk;
    pCharacteristic->setValue(framed.c_str());
    pCharacteristic->notify();
    delay(20);
  }
}

#if ENABLE_LORA
void loraSendJSON(const String &payload) {
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();
  Serial.println("[LORA] Packet transmitted.");
}

void loraPoll() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String received;
  while (LoRa.available()) {
    received += (char)LoRa.read();
  }

  JsonDocument doc;
  if (deserializeJson(doc, received)) return;

  String packetId = doc["packet_id"] | "";
  if (isDuplicatePacket(packetId)) return;

  uint32_t myNodeId = mesh.getNodeId();
  uint32_t targetNode = doc["target_node"] | 0;
  int ttl = doc["ttl"] | LORA_MAX_TTL;

  if (IS_RECEIVER_GATEWAY || targetNode == myNodeId) {
    handlePacketAtDestination(doc);
    return;
  }

  ttl -= 1;
  if (ttl <= 0) return;
  doc["ttl"] = ttl;

  JsonArray routeTrace = doc["route_trace"];
  if (!routeTrace.isNull()) {
    routeTrace.add(myNodeId);
  }

  String outputPayload;
  serializeJson(doc, outputPayload);

  mesh.sendSingle(targetNode, outputPayload);
  loraSendJSON(outputPayload);
  
  blinkLED(1, 50);
}
#endif