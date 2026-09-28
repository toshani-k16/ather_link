#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Set ENABLE_LORA to 1 once you have an SX127x module wired up (see LORA PINS below).
// Set to 0 to build/test the WiFi-mesh + BLE portion on boards without a LoRa radio.
#define ENABLE_LORA 1
#if ENABLE_LORA
  #include <SPI.h>
  #include <LoRa.h>
#endif

// ================= NODE CONFIGURATION =================
// Set to 'true' ONLY on the central Receiver/Dashboard ESP32 connected to the PC.
// Set to 'false' for handheld victim nodes and intermediate relays.
const bool IS_RECEIVER_GATEWAY = false;

// Central Gateway Receiver Node ID.
// This is painlessMesh's runtime-generated ID (derived from the gateway's chip MAC),
// NOT something you choose. Workflow:
//   1. Flash the gateway with IS_RECEIVER_GATEWAY = true.
//   2. Read the "Node ID:" value it prints over Serial at boot.
//   3. Paste that value here and re-flash every other (non-gateway) node.
// If you ever swap the physical gateway board, repeat this and re-flash all nodes.
uint32_t RECEIVER_NODE_ID = 381048291;

#define MESH_PREFIX        "AetherMesh"
#define MESH_PASSWORD      "AetherMeshPassword"
#define MESH_PORT          5555
#define MESH_CHANNEL       6        // Lock channel to prevent BLE/Wi-Fi connection drops

// ================= HARDWARE PINS (STANDARD ESP32) =================
#define BUTTON_PIN         4        // SOS Push button (Connected to GND)
#define GPS_RX_PIN         16       // GPS Module TX -> ESP32 RX2 (Pin 16)
#define GPS_TX_PIN         17       // GPS Module RX -> ESP32 TX2 (Pin 17)

// LoRa (SX1276/RA-02 style module) pins - adjust to match your wiring.
// These match the common "Dragino/RA-02 breakout on a bare ESP32 devkit" pinout.
#define LORA_SCK_PIN       18
#define LORA_MISO_PIN      19
#define LORA_MOSI_PIN      23
#define LORA_SS_PIN        5
#define LORA_RST_PIN       14
#define LORA_DIO0_PIN      2
#define LORA_FREQUENCY     433E6   // Change to 868E6 or 915E6 to match your module/region
#define LORA_MAX_TTL       8       // Max hops a LoRa flood packet can travel before being dropped

// Static Fallback GPS (Used if indoors / no satellite lock)
const double FALLBACK_LAT = 12.9716;
const double FALLBACK_LNG = 77.5946;

// ================= BLE CONFIGURATION =================
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// Real-world BLE notify() payloads are capped by the negotiated ATT MTU (default only
// 23 bytes, ~20 usable). We request a much larger MTU below, but a phone app can still
// refuse it, so outgoing JSON is always chunked defensively. The companion app must
// reassemble notifications shaped "<chunkIndex>/<totalChunks>:<data>".
#define BLE_REQUESTED_MTU   247
#define BLE_CHUNK_SIZE      180

// ================= GLOBAL OBJECTS =================
painlessMesh mesh;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Use UART2 on standard ESP32

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 300;
bool lastButtonState = HIGH;

// ================= DUPLICATE-PACKET SUPPRESSION =================
// painlessMesh's own sendSingle() already routes through its internal tree, so the
// only place duplicates can arise is a broadcast/LoRa-flood fallback landing on
// several nodes at once. Every packet in this sketch carries a "packet_id"; each
// node remembers the last N ids it has already acted on so it never re-processes or
// re-floods the same packet twice.
#define DEDUP_BUFFER_SIZE 24
String recentPacketIds[DEDUP_BUFFER_SIZE];
int dedupIndex = 0;

bool isDuplicatePacket(const String &packetId) {
  if (packetId.length() == 0) return false; // no id to key on, let it through
  for (int i = 0; i < DEDUP_BUFFER_SIZE; i++) {
    if (recentPacketIds[i] == packetId) return true;
  }
  recentPacketIds[dedupIndex] = packetId;
  dedupIndex = (dedupIndex + 1) % DEDUP_BUFFER_SIZE;
  return false;
}

// Function Declarations
void sendSOS();
void routeBluetoothJSON(String jsonPayloadStr);
void receivedCallback(uint32_t from, String &msg);
void handlePacketAtDestination(JsonDocument &doc);
void bleSendJSON(const String &payload);
void ensurePacketId(JsonDocument &doc);
#if ENABLE_LORA
void loraSendJSON(const String &payload);
void loraPoll();
#endif

// ================= BLE CALLBACK HANDLERS =================
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

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random()); // avoid identical SOS-ID sequences on every reboot

  // 1. Initialize Hardware Pins & GPS Serial
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // 2. Initialize painlessMesh Network on locked Wi-Fi Channel 6
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

  // 3. Initialize Standard ESP32 BLE GATT Server
  BLEDevice::init("AetherNode_ESP32");
  BLEDevice::setMTU(BLE_REQUESTED_MTU); // ask the phone to negotiate a bigger ATT MTU
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

  // 4. Initialize LoRa radio for genuine long-range hops between ESP32 nodes.
  // This is the physical layer your pitch deck's "ESP32 + LoRa Mesh Nodes" block
  // actually needs - painlessMesh alone only gives you WiFi range (tens of metres).
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

// ================= MAIN LOOP =================
void loop() {
  mesh.update(); // Keep mesh topology active

  // Parse GPS byte stream
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

#if ENABLE_LORA
  loraPoll();
#endif

  // Monitor SOS Button
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

// Adds a packet_id if one isn't already present (e.g. packets arriving fresh from
// the phone over BLE), so every packet flowing through the system can be deduped.
void ensurePacketId(JsonDocument &doc) {
  if (!doc["packet_id"].is<const char*>()) {
    doc["packet_id"] = "PKT-" + String(random(100000, 999999));
  }
}

// ================= BLE ROUTING ENGINE =================
void routeBluetoothJSON(String jsonPayloadStr) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonPayloadStr);

  if (err) {
    Serial.println("[BLE ERROR] Failed to parse incoming phone JSON.");
    return;
  }

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

  String outputPayload;
  serializeJson(doc, outputPayload);

  // Try the WiFi mesh first (short range, reliable).
  if (mesh.sendSingle(target, outputPayload)) {
    Serial.printf("[MESH] Phone BLE packet routed toward Target Node: %u\n", target);
  } else {
#if ENABLE_LORA
    Serial.println("[MESH WARNING] Targeted path unavailable. Falling back to LoRa...");
    loraSendJSON(outputPayload);
#else
    Serial.println("[MESH WARNING] Targeted path unavailable. Fallback broadcast...");
    mesh.sendBroadcast(outputPayload);
#endif
  }
}

// ================= HARDWARE SOS BUTTON ROUTING =================
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

  isDuplicatePacket(doc["packet_id"].as<String>()); // remember our own packet immediately

  String payload;
  serializeJson(doc, payload);

  if (mesh.sendSingle(RECEIVER_NODE_ID, payload)) {
    Serial.printf("[MESH] Hardware SOS packet routed toward Gateway Node: %u\n", RECEIVER_NODE_ID);
  } else {
#if ENABLE_LORA
    Serial.println("[MESH WARNING] Target path unavailable. Falling back to LoRa...");
    loraSendJSON(payload);
#else
    Serial.println("[MESH WARNING] Target path unavailable. Fallback broadcast...");
    mesh.sendBroadcast(payload);
#endif
  }
}

// ================= MESH RECEIVE CALLBACK =================
void receivedCallback(uint32_t from, String &msg) {
  JsonDocument doc;
  if (deserializeJson(doc, msg)) return;

  String packetId = doc["packet_id"] | "";
  if (isDuplicatePacket(packetId)) {
    return; // already handled this exact packet - stops broadcast-fallback duplication
  }

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
  }
}

// Common "packet has arrived" handling shared by the WiFi-mesh and LoRa paths.
void handlePacketAtDestination(JsonDocument &doc) {
  Serial.println("\n================ [TARGET REACHED] ================");
  Serial.println("Packet Payload     :");
  serializeJsonPretty(doc, Serial);
  Serial.println("\n==================================================");

  if (deviceConnected) {
    String output;
    serializeJson(doc, output);
    bleSendJSON(output);
  }
}

// Chunked BLE notify so payloads survive even when the phone won't negotiate a
// bigger ATT MTU. Companion app must reassemble "<idx>/<total>:<chunk>" frames.
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
    delay(20); // give the BLE stack time to flush before the next notification
  }
}

// ================= LORA LONG-RANGE LAYER =================
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
  if (deserializeJson(doc, received)) {
    Serial.println("[LORA ERROR] Failed to parse incoming packet.");
    return;
  }

  String packetId = doc["packet_id"] | "";
  if (isDuplicatePacket(packetId)) {
    return; // already seen this packet on a previous hop - stop the flood here
  }

  uint32_t myNodeId = mesh.getNodeId();
  uint32_t targetNode = doc["target_node"] | 0;
  int ttl = doc["ttl"] | LORA_MAX_TTL;

  Serial.printf("[LORA] Packet received (RSSI %d).\n", LoRa.packetRssi());

  if (IS_RECEIVER_GATEWAY || targetNode == myNodeId) {
    handlePacketAtDestination(doc);
    return;
  }

  // Not for us: decrement TTL and re-flood so it can hop toward the gateway.
  ttl -= 1;
  if (ttl <= 0) {
    Serial.println("[LORA] Packet dropped - TTL expired.");
    return;
  }
  doc["ttl"] = ttl;

  JsonArray routeTrace = doc["route_trace"];
  if (!routeTrace.isNull()) {
    routeTrace.add(myNodeId);
  }

  String outputPayload;
  serializeJson(doc, outputPayload);

  // Also try the local WiFi mesh in case the target is reachable that way,
  // then re-flood over LoRa regardless so other LoRa-only relays still get it.
  mesh.sendSingle(targetNode, outputPayload);
  loraSendJSON(outputPayload);
  Serial.printf("[LORA RELAY] Forwarded packet toward Target %u (TTL now %d)\n", targetNode, ttl);
}
#endif
