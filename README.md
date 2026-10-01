# AetherLink: Decentralized Off-Grid Mesh Intelligence

**AetherLink** is a decentralized emergency communication system designed to keep critical disaster messages moving even when traditional communication infrastructure is unavailable.

During disasters such as floods, earthquakes, fires, or large-scale network failures, cellular networks and internet connectivity can become unreliable or completely unavailable. AetherLink addresses this problem by allowing nearby devices to communicate and relay emergency messages through multiple communication layers.

##  How AetherLink Works

AetherLink uses a **three-level communication fallback system**:

1. **Internet / Network** — When connectivity is available, emergency alerts are sent directly to the rescue server.
2. **Bluetooth Low Energy (BLE)** — If internet connectivity is unavailable, nearby smartphones can exchange and relay emergency messages from one device to another.
3. **ESP32 Mesh Network** — As a further fallback, ESP32-based nodes can form a decentralized mesh network and forward messages across multiple hops.

At the center of the system is a lightweight **local AI module** that is based on **Navive Bayes Theorem** processes a user's natural-language emergency request and converts it into a compact, structured JSON message. This reduces the amount of data that needs to be transmitted while preserving the important information required by rescue teams.

### Example

A user could send:

> "My grandmother needs insulin near the main gate."

The local AI can convert this into a structured emergency message such as:

```json
{
  "category": "MEDICAL",
  "location": "MAIN_GATE",
  "need": "INSULIN",
  "priority": "HIGH"
}
```

The message can then be transmitted through the available communication layer and relayed by intermediate devices until it reaches a rescue-team gateway or destination.

##  Decentralized Communication

AetherLink does not depend on a single communication path. Instead, devices can act as **senders, receivers, and relays**, creating an ad-hoc communication network.

```text
User
  │
  ▼
Local AI
  │
  ▼
Emergency JSON
  │
  ├── Internet ───────────────► Rescue Server
  │
  ├── BLE ──► Phone ──► Phone ──► Gateway
  │
  └── ESP32 Mesh ─► Node ─► Node ─► Gateway
                                      │
                                      ▼
                                Rescue Dashboard
```

This architecture allows AetherLink to continue functioning even when parts of the communication infrastructure fail.

##  Project Goal

The goal of AetherLink is to provide a **low-bandwidth, decentralized, and resilient emergency communication platform** that can help connect people in disaster-affected areas with rescue and response teams.

The project combines:

*  **Local AI** for emergency message extraction
*  **Android** for user communication and BLE relay
*  **BLE** for device-to-device communication
*  **ESP32 mesh networking** for extended off-grid communication
*  **Flask backend** for receiving and processing alerts
*  **Rescue dashboard** for monitoring incoming emergency requests

Additionally we have a hand-held device which acts as a SOS button and as a node so that when there is a rescue/emergency situation the person can simply press a SOS button in our hand-held device which immediately sends his live precise location to the control room this is particularly useful in places where there is low connectivity or little to population as a result the message OR the user is in a VERY critical condition as a result he/she cannot type a prompt in his mobile....In situation like these the hand-held AetherLink device comes into play. 

AetherLink is built around a simple principle:

> **When infrastructure goes down, the network should not have to go down with it.**

