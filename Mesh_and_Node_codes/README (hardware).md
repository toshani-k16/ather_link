# AetherLink Gateway: ESP32 Mesh SOS Node

One firmware runs on every node. A node can be a handheld SOS device, a relay, or the central gateway, depending on a single flag. Nodes talk to each other over a WiFi mesh (painlessMesh), take input from a smartphone over Bluetooth Low Energy (BLE), and can optionally fall back to long-range LoRa radio. The gateway hands everything to a Flask server on a laptop.

---

## 1. Hardware

| Component | Purpose | Required |
|---|---|---|
| ESP32 dev board (one per node) | Main controller: WiFi mesh, BLE, GPS parsing | Yes |
| Push button (momentary) | Hardware SOS / panic trigger | Yes |
| GPS module (NMEA over UART, e.g. NEO-6M) | Location for SOS packets | Recommended |
| SX127x LoRa module, 433 MHz | Long-range fallback link | Optional (`ENABLE_LORA`) |
| Smartphone with the companion app | Sends/receives JSON over BLE | Optional |
| Laptop / phone hotspot running the Flask server | Receives packets from the gateway | Gateway only |

### Wiring

**SOS button**

| Button | ESP32 |
|---|---|
| One leg | GPIO 4 |
| Other leg | GND |

The code uses `INPUT_PULLUP`, so no external resistor is needed. The pin reads HIGH when idle and LOW when pressed.

**GPS module** (UART2 at 9600 baud)

| GPS | ESP32 |
|---|---|
| TX | GPIO 16 (ESP32 RX) |
| RX | GPIO 17 (ESP32 TX) |
| VCC | 3.3 V or 5 V (check your module) |
| GND | GND |

TX and RX are crossed: the module's TX goes to the ESP32's RX pin.

**LoRa SX127x** (only if `ENABLE_LORA` is set to `1`)

| LoRa | ESP32 |
|---|---|
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| NSS / CS | GPIO 5 |
| RST | GPIO 14 |
| DIO0 | GPIO 2 |
| VCC | **3.3 V only** |
| GND | GND |

The radio runs at 433 MHz. Attach an antenna before powering the module, because transmitting without one can damage it.

---

## 2. Node roles

Set these at the top of the code before flashing each board:

| Setting | Gateway node | Handheld / relay node |
|---|---|---|
| `IS_RECEIVER_GATEWAY` | `true` | `false` |
| `RECEIVER_NODE_ID` | Its own mesh ID | The **gateway's** mesh ID |

Each board prints its mesh node ID on the serial monitor at boot. Flash the gateway first, read its ID, and put that number into `RECEIVER_NODE_ID` on every other node. The gateway is also made the mesh root (`setRoot(true)`).

---

## 3. Gateway node (hardware and operation)

The gateway is the single ESP32 that connects the mesh to the outside world. It is the only node that needs WiFi access to the server.

### System layout

```
 [Handheld / relay nodes] --- painlessMesh (WiFi) ---+
 [Smartphone] ---- BLE ---- [Any node]               |
 [LoRa nodes] ---- 433 MHz radio (optional) ---------+--> [GATEWAY ESP32] --WiFi--> [Laptop: Flask :5000]
                                                          |                        POST /api/endpoint
                                                          +-- BLE notify --> [Gateway's own phone]
                                                          +-- Serial (USB) --> "SERVER_JSON:{...}"
```

### Gateway hardware

| Item | Details |
|---|---|
| ESP32 board | Powered over USB or a 5 V supply. USB also gives the serial monitor at 115200 baud. |
| WiFi link | Joins the network set in `WIFI_SSID` / `WIFI_PASSWORD` (a phone hotspot works). The laptop running Flask must be on the same network. |
| Laptop | Runs the Flask server on port 5000. `SERVER_URL` must hold the laptop's current IPv4 address, which changes on hotspots. |
| Button, GPS, LoRa | Same wiring as in section 1. They are optional on the gateway, because the same firmware runs on every node. A button press on the gateway is handled locally. |

Because one ESP32 has only one radio, the gateway runs the mesh access point and the WiFi station together (`WIFI_AP_STA`). The mesh channel (`MESH_CHANNEL`) must match the channel of the WiFi network it joins, otherwise one of the two links will drop.

### How the gateway differs from other nodes

When `IS_RECEIVER_GATEWAY` is `true`:

- The node is set as the mesh **root** (`setRoot(true)`), so the other nodes treat it as the top of the network.
- It **never relays**. Every packet that reaches it from the mesh, from BLE or from LoRa is treated as having arrived at its destination, even when `target_node` is another node.
- Its own SOS button presses and phone BLE packets skip the mesh entirely and go straight to `handlePacketAtDestination()`.

### What the gateway does with each packet

1. The packet is appended with its route and pretty-printed on the serial monitor under `[TARGET REACHED]`.
2. The packet is serialized to one line of JSON and printed as `SERVER_JSON:<json>`. This is the data meant for the Flask server (see section 9 for how it gets there).
3. If a smartphone is connected to the gateway over BLE, the same packet is sent to it as chunked notifications (see section 7).

### Server side

The Flask server must accept `POST /api/endpoint` with `Content-Type: application/json`. The body is the packet JSON, for example:

```json
{
  "packet_id": "SOS-123456",
  "source_type": "HANDHELD_BUTTON",
  "source_node": 123456789,
  "target_node": 730528681,
  "ttl": 8,
  "data": { "category": "SOS", "item": "Panic Button Triggered", "priority": "CRITICAL" },
  "gps": { "lat": 12.9716, "lng": 77.5946, "accuracy": "STATIC_FALLBACK" },
  "route_trace": [123456789, 730528681]
}
```

### Bringing up a gateway

1. Set `IS_RECEIVER_GATEWAY = true` and flash the board.
2. Open the serial monitor and note the printed `Gateway Node ID`.
3. Check that `[WIFI] Connected!` appears along with an IP address. If it says it failed, the mesh and BLE still work but nothing can reach the server.
4. Put the gateway's node ID into `RECEIVER_NODE_ID` on every other node and flash them with `IS_RECEIVER_GATEWAY = false`.
5. Press the button on a handheld node and confirm that a `[TARGET REACHED]` block and a `SERVER_JSON:` line appear on the gateway.

---

## 4. What happens at boot

1. **Serial and random seed.** Serial runs at 115200 baud. The random generator is seeded from the ESP32 hardware RNG, which keeps packet IDs from repeating across reboots.
2. **Hardware init.** The button pin is set to pull-up and UART2 starts listening to the GPS.
3. **WiFi.** The ESP32 tries to join the configured network for up to 15 seconds. If that fails, mesh and BLE keep working without it.
4. **painlessMesh.** The node joins the `AetherMesh` network on port 5555, channel 6, in AP+STA mode, and registers `receivedCallback` for incoming mesh messages.
5. **BLE GATT server.** The node advertises as `AetherNode_ESP32` with one service and one characteristic that supports read, write and notify. The requested MTU is 247 bytes.
6. **LoRa (optional).** If enabled, SPI and the radio are initialised at 433 MHz.
7. **Ready banner.** The node ID and WiFi IP are printed.

---

## 5. Main loop

The loop runs continuously and does four things:

- `mesh.update()` keeps the mesh alive and processes messages.
- GPS bytes are fed into TinyGPS++ so the latest fix is always available.
- If LoRa is enabled, `loraPoll()` checks for incoming radio packets.
- The button is checked on its falling edge (HIGH to LOW) with a 300 ms debounce. A valid press calls `sendSOS()`.

---

## 6. Data flow

### A. Hardware SOS button

1. The button is pressed and `sendSOS()` builds a JSON packet containing:
   - `packet_id` (`SOS-xxxxxx`), `source_type: HANDHELD_BUTTON`, `source_node`, `target_node`, `ttl`
   - `data`: category `SOS`, item `Panic Button Triggered`, priority `CRITICAL`
   - `gps`: latitude and longitude, with `accuracy` set to `GPS_LOCK` if the GPS has a valid fix. Otherwise it uses the hard-coded fallback position (12.9716, 77.5946) and sets `accuracy` to `STATIC_FALLBACK`.
   - `route_trace`: list of node IDs the packet has passed through
2. If this node is the gateway, the packet is handled locally.
3. Otherwise it is sent through the mesh directly to the gateway (`sendSingle`).
4. If the mesh cannot deliver it, the node falls back to **LoRa** (if enabled) or to a **mesh broadcast**.

### B. Smartphone over BLE

1. The phone connects to the ESP32 and writes a JSON string to the characteristic.
2. `routeBluetoothJSON()` parses it, adds a `packet_id` if missing, tags it `SMARTPHONE_BLE`, sets a default TTL, defaults the target to the gateway, and appends this node's ID to `route_trace`.
3. It is then delivered locally (if this node is the target) or forwarded through the mesh, with the same LoRa or broadcast fallback.

### C. Relaying and delivery

When a mesh message arrives, `receivedCallback()` does this:

1. Parse the JSON. Invalid packets are ignored.
2. Drop duplicates (see section 7).
3. Append this node's ID to `route_trace`.
4. If this node is the gateway or the target, call `handlePacketAtDestination()`. Otherwise forward the packet toward `target_node`.

### D. At the destination

`handlePacketAtDestination()` pretty-prints the packet on the serial monitor. On the gateway it also prints a `SERVER_JSON:` line containing the packet as one line of JSON. If a phone is connected over BLE, the packet is also pushed to it as a notification.

### E. LoRa path (if enabled)

Incoming radio packets are checked for validity and duplicates. If the packet is for this node or this node is the gateway, it is delivered. Otherwise the TTL is decremented. If it reaches 0 the packet is dropped. If not, the node appends its ID and re-forwards the packet over both the mesh and LoRa. The TTL starts at 8, which stops packets bouncing around forever.

---

## 7. Supporting mechanisms

**Duplicate suppression.** The last 24 packet IDs are kept in a circular buffer. A packet whose ID is already in the buffer is dropped. This matters because the same packet may arrive by more than one path (mesh, broadcast, LoRa).

**BLE chunking.** A single BLE notification is limited in size, so `bleSendJSON()` splits outgoing JSON into 180-byte chunks. Each chunk is prefixed with `index/total:`, for example `2/3:...`, and sent with a 20 ms gap. The phone app must reassemble the chunks in order.

**BLE reconnect.** When the phone disconnects, advertising restarts automatically so it can reconnect.

**GPS fallback.** If the GPS has no fix (for example indoors), packets still go out with the static fallback coordinates, marked `STATIC_FALLBACK`.

---

## 8. Configuration checklist

- Set `WIFI_SSID` and `WIFI_PASSWORD` (the gateway needs WiFi to reach the server). Avoid committing real credentials to a public repo.
- Set `SERVER_URL` to the laptop's IPv4 address and Flask port.
- Set `IS_RECEIVER_GATEWAY` and `RECEIVER_NODE_ID` as described in section 2.
- Make sure `MESH_CHANNEL` matches the WiFi network's channel on the gateway, because the ESP32 has only one radio.
- Set `ENABLE_LORA` to `1` only after the radio is wired up.
- Replace `FALLBACK_LAT` and `FALLBACK_LNG` with a location that makes sense for your deployment.


