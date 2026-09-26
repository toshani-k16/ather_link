#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ================= USER CONFIGURATION =================
// Set this to 'true' ONLY on the central Receiver / Dashboard ESP32 board.
// Set to 'false' for all victim/relay mesh nodes.
const bool IS_RECEIVER_GATEWAY = false; 

//set the Receiver's exact 32-bit Node ID here.

uint32_t RECEIVER_NODE_ID = 0; 

#define MESH_PREFIX     "AetherMesh"
#define MESH_PASSWORD   "AetherMeshPassword"
#define MESH_PORT       5555

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
// =======================================================

painlessMesh mesh;
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

void routeTargetedMessage(String jsonPayloadStr);
void receivedCallback(uint32_t from, String &msg);

// BLE Callbacks for Smartphone Connectivity
class BLECallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String rxValue = pCharacteristic->getValue().c_str();
        if (rxValue.length() > 0) {
            Serial.println("[BLE] Received packet from Smartphone:");
            Serial.println(rxValue);
            
            // Inject phone request into targeted routing mechanism
            routeTargetedMessage(rxValue);
        }
    }
};

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      pServer->getAdvertising()->start();
    }
};

void setup() {
  Serial.begin(115200);

  // 1. Initialize Mesh Network
  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  // If this node is the main receiver/gateway, set it as the root
  if (IS_RECEIVER_GATEWAY) {
    mesh.setRoot(true);
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] Operating as CENTRAL RECEIVER. Node ID: %u\n", mesh.getNodeId());
  } else {
    mesh.setContainsRoot(true);
    Serial.printf("[SYSTEM] Operating as RELAY/SENDER NODE. Node ID: %u\n", mesh.getNodeId());
  }

  // 2. Initialize BLE Interface
  BLEDevice::init("AetherNode");
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
  BLEDevice::startAdvertising();
}

void loop() {
  mesh.update(); // Maintain targeted mesh links and routing tables
}

// Function to attach Route Tracing & perform Targeted Unicast Routing
void routeTargetedMessage(String jsonPayloadStr) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, jsonPayloadStr);
  
  if (err) {
    Serial.println("[ERROR] Failed to parse input JSON");
    return;
  }

  // Append current node ID to the hop path array
  JsonArray routeTrace = doc["route_trace"].as<JsonArray>();
  if (!routeTrace) {
    routeTrace = doc.createNestedArray("route_trace");
  }
  routeTrace.add(mesh.getNodeId());

  // Determine Target Destination ID
  uint32_t target = doc["target_node"] | RECEIVER_NODE_ID;
  
  // Dynamic discovery fallback: If target ID is unset, route to mesh root
  if (target == 0) {
    // Search active connections for the designated root/receiver node
    target = mesh.getNodeId(); // Default if isolated
  }

  String outputPayload;
  serializeJson(doc, outputPayload);

  // TARGETED ROUTING (Unicast)
  // Send specifically to target node without flooding the whole network
  bool success = mesh.sendSingle(target, outputPayload);
  
  if (success) {
    Serial.printf("[MESH] Unicast packet forwarded towards Target Node: %u\n", target);
  } else {
    Serial.println("[MESH ERROR] Targeted route failed. Fallback to broadcast...");
    mesh.sendBroadcast(outputPayload); // Fallback if direct path is unavailable
  }
}

// Callback invoked when receiving mesh data from another ESP32
void receivedCallback(uint32_t from, String &msg) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) return;

  uint32_t myNodeId = mesh.getNodeId();
  uint32_t targetNode = doc["target_node"] | 0;

  // Append this node to the route history trace
  JsonArray routeTrace = doc["route_trace"].as<JsonArray>();
  if (routeTrace) {
    routeTrace.add(myNodeId);
  }

  // Check if current node is the final destination
  if (IS_RECEIVER_GATEWAY || targetNode == myNodeId) {
    Serial.println("\n================ [TARGET REACHED] ================");
    Serial.printf("Source Origin Node : %u\n", from);
    Serial.println("Packet Payload     :");
    serializeJsonPretty(doc, Serial);
    Serial.println("\n==================================================");

    // Relay to Dashboard via connected BLE device if active
    if (deviceConnected) {
      String output;
      serializeJson(doc, output);
      pCharacteristic->setValue(output.c_str());
      pCharacteristic->notify();
      Serial.println("[BLE] Relayed data to local Dashboard display.");
    }
  } else {
    // If not the target, re-forward toward the destination target
    String outputPayload;
    serializeJson(doc, outputPayload);
    mesh.sendSingle(targetNode, outputPayload);
    Serial.printf("[RELAY] Relayed packet from Node %u towards Target Node %u\n", from, targetNode);
  }
}