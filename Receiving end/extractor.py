import serial
import requests
import json

# ============================================================
# CONFIGURATION
# ============================================================

COM_PORT = "COM3"       # CHANGE THIS
BAUD_RATE = 115200

FLASK_URL = "http://10.143.240.171:5000/api/endpoint"


# ============================================================
# CONNECT TO ESP32
# ============================================================

print("Connecting to ESP32...")

ser = serial.Serial(
    COM_PORT,
    BAUD_RATE,
    timeout=1
)

print("Connected to ESP32!")
print("Waiting for AetherLink packets...\n")


# ============================================================
# MAIN LOOP
# ============================================================

while True:

    try:

        line = ser.readline().decode(
            "utf-8",
            errors="ignore"
        ).strip()

        if not line:
            continue


        print("[ESP32]", line)


        # ----------------------------------------------------
        # Look for JSON packets intended for Flask
        # ----------------------------------------------------

        if line.startswith("SERVER_JSON:"):

            json_string = line[
                len("SERVER_JSON:"):
            ]


            print("\n================================")
            print("JSON RECEIVED FROM ESP32")
            print("================================")

            print(json_string)


            # ------------------------------------------------
            # Validate JSON
            # ------------------------------------------------

            try:

                data = json.loads(json_string)

            except json.JSONDecodeError:

                print("[ERROR] Invalid JSON!")
                continue


            # ------------------------------------------------
            # Send JSON to Flask
            # ------------------------------------------------

            print("[BRIDGE] Sending JSON to Flask...")


            try:

                response = requests.post(
                    FLASK_URL,
                    json=data,
                    timeout=5
                )


                print(
                    "[FLASK] HTTP:",
                    response.status_code
                )

                print(
                    "[FLASK] Response:",
                    response.text
                )


            except requests.RequestException as e:

                print(
                    "[FLASK ERROR]",
                    e
                )


            print("================================\n")


    except KeyboardInterrupt:

        print("\nStopping bridge...")

        ser.close()

        break