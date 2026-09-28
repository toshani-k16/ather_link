from flask import Flask, request, jsonify, render_template
import json
import os
from datetime import datetime, timezone
import uuid

app = Flask(__name__)

DATA_FILE = "alerts.json"

def load_alerts():
    if os.path.exists(DATA_FILE):
        with open(DATA_FILE, "r") as f:
            return json.load(f)
    return []

def save_alerts(alerts):
    with open(DATA_FILE, "w") as f:
        json.dump(alerts, f, indent=2)

@app.route("/api/endpoint", methods=["POST"])
def receive_json():
    """
    This is the final landing point for a message —
    whether it arrived via direct Wi-Fi/network send,
    or via a BLE mesh hop that reached a phone with internet.
    """
    data = request.get_json(force=True, silent=True)
    if data is None:
        return jsonify({"status": "error", "message": "No valid JSON received"}), 400

    alerts = load_alerts()

    record = {
        "record_id": str(uuid.uuid4()),
        "received_at": datetime.now(timezone.utc).isoformat(),
        "source_ip": request.remote_addr,
        "payload": data,
        "status": "new"  # "new" | "attended"
    }
    alerts.insert(0, record)  # newest first
    alerts = alerts[:200]  # keep last 200 to avoid unbounded growth
    save_alerts(alerts)

    print(f"[{record['received_at']}] Received from {record['source_ip']}: {data}")

    return jsonify({"status": "success", "message": "Alert received", "record_id": record["record_id"]}), 200

@app.route("/api/alerts", methods=["GET"])
def get_alerts():
    """Used by the dashboard page to poll for updates."""
    return jsonify(load_alerts())

@app.route("/api/alerts/<record_id>/status", methods=["PATCH"])
def update_status(record_id):
    """Toggle an alert between 'new' and 'attended'."""
    data = request.get_json(force=True, silent=True) or {}
    new_status = data.get("status")
    if new_status not in ("new", "attended"):
        return jsonify({"status": "error", "message": "status must be 'new' or 'attended'"}), 400

    alerts = load_alerts()
    for a in alerts:
        if a["record_id"] == record_id:
            a["status"] = new_status
            save_alerts(alerts)
            return jsonify({"status": "success", "record_id": record_id, "new_status": new_status}), 200

    return jsonify({"status": "error", "message": "record not found"}), 404

@app.route("/api/alerts/<record_id>", methods=["DELETE"])
def delete_alert(record_id):
    """Permanently remove an alert (resolved, duplicate, or false report)."""
    alerts = load_alerts()
    remaining = [a for a in alerts if a["record_id"] != record_id]

    if len(remaining) == len(alerts):
        return jsonify({"status": "error", "message": "record not found"}), 404

    save_alerts(remaining)
    return jsonify({"status": "success", "record_id": record_id}), 200

@app.route("/")
def dashboard():
    return render_template("dashboard.html")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)