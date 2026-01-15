#!/usr/bin/env python3
"""
Home Assistant Proxy for FluidNC

Provides short URLs for controlling Home Assistant devices from GCode commands.
FluidNC has a 255-character command line limit, so this proxy handles
authentication server-side with the JWT token.

Usage from FluidNC:
    $HTTP=http://192.168.1.100:5050/laser/water/on
    $HTTP=http://192.168.1.100:5050/laser/air/on

Note: Use IP addresses instead of hostnames - FluidNC may not resolve DNS names.
"""

import logging
import os
import sys
from pathlib import Path

import requests
from flask import Flask, jsonify

# Configuration from environment variables
HA_URL = os.environ.get("HA_URL", "http://localhost:8123")
TOKEN_FILE = Path(os.environ.get("HA_TOKEN_FILE", ".ha_token"))
PORT = int(os.environ.get("PORT", 5050))

# Entity mappings for laser controls
# Customize these to match your Home Assistant entity IDs
LASER_ENTITIES = {
    "water": "switch.laser_water_cooling",    # Water cooling pump
    "air": "switch.laser_air_assist",         # Air assist compressor
    "vent": "switch.laser_ventilation",       # Ventilation/extraction
    "power": "switch.laser_main_power",       # Main power relay
}

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)]
)
log = logging.getLogger(__name__)

app = Flask(__name__)


def get_token() -> str:
    """Read HA token from file."""
    if TOKEN_FILE.exists():
        return TOKEN_FILE.read_text().strip()
    raise RuntimeError(f"Token file not found: {TOKEN_FILE}")


def ha_request(method: str, endpoint: str, json_data: dict = None) -> dict:
    """Make authenticated request to Home Assistant API."""
    token = get_token()
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    }
    url = f"{HA_URL}/api/{endpoint}"

    try:
        response = requests.request(
            method, url, headers=headers, json=json_data, timeout=10
        )
        response.raise_for_status()
        return {"success": True, "data": response.json() if response.text else {}}
    except requests.exceptions.RequestException as e:
        log.error(f"HA request failed: {e}")
        return {"success": False, "error": str(e)}


def call_service(domain: str, service: str, entity_id: str) -> dict:
    """Call a Home Assistant service."""
    log.info(f"Calling {domain}.{service} on {entity_id}")
    return ha_request("POST", f"services/{domain}/{service}", {"entity_id": entity_id})


def get_entity_state(entity_id: str) -> dict:
    """Get state of an entity."""
    result = ha_request("GET", f"states/{entity_id}")
    if result["success"]:
        state_data = result["data"]
        return {
            "entity_id": entity_id,
            "state": state_data.get("state"),
            "friendly_name": state_data.get("attributes", {}).get("friendly_name"),
        }
    return {"entity_id": entity_id, "state": "unknown", "error": result.get("error")}


# Health check
@app.route("/health", methods=["GET"])
def health():
    """Health check endpoint."""
    return jsonify({"status": "ok", "service": "laser-ha-proxy"})


# Laser control endpoints
@app.route("/laser/<device>/on", methods=["GET", "POST"])
def laser_on(device: str):
    """Turn on a laser device."""
    entity_id = LASER_ENTITIES.get(device)
    if not entity_id:
        return jsonify({"success": False, "error": f"Unknown device: {device}"}), 404

    result = call_service("switch", "turn_on", entity_id)
    return jsonify(result)


@app.route("/laser/<device>/off", methods=["GET", "POST"])
def laser_off(device: str):
    """Turn off a laser device."""
    entity_id = LASER_ENTITIES.get(device)
    if not entity_id:
        return jsonify({"success": False, "error": f"Unknown device: {device}"}), 404

    result = call_service("switch", "turn_off", entity_id)
    return jsonify(result)


@app.route("/laser/<device>/toggle", methods=["GET", "POST"])
def laser_toggle(device: str):
    """Toggle a laser device."""
    entity_id = LASER_ENTITIES.get(device)
    if not entity_id:
        return jsonify({"success": False, "error": f"Unknown device: {device}"}), 404

    result = call_service("switch", "toggle", entity_id)
    return jsonify(result)


@app.route("/laser/<device>/status", methods=["GET"])
def laser_device_status(device: str):
    """Get status of a specific laser device."""
    entity_id = LASER_ENTITIES.get(device)
    if not entity_id:
        return jsonify({"success": False, "error": f"Unknown device: {device}"}), 404

    state = get_entity_state(entity_id)
    return jsonify({"success": True, **state})


@app.route("/laser/status", methods=["GET"])
def laser_status():
    """Get status of all laser devices."""
    states = {}
    for device, entity_id in LASER_ENTITIES.items():
        state = get_entity_state(entity_id)
        states[device] = state.get("state", "unknown")

    return jsonify({"success": True, "devices": states})


# Convenience endpoints - control multiple devices at once
@app.route("/laser/all/on", methods=["GET", "POST"])
def laser_all_on():
    """Turn on water and air for laser operation."""
    results = {}
    for device in ["water", "air"]:
        entity_id = LASER_ENTITIES.get(device)
        if entity_id:
            result = call_service("switch", "turn_on", entity_id)
            results[device] = "on" if result["success"] else "failed"

    return jsonify({"success": True, "results": results})


@app.route("/laser/all/off", methods=["GET", "POST"])
def laser_all_off():
    """Turn off water and air."""
    results = {}
    for device in ["water", "air"]:
        entity_id = LASER_ENTITIES.get(device)
        if entity_id:
            result = call_service("switch", "turn_off", entity_id)
            results[device] = "off" if result["success"] else "failed"

    return jsonify({"success": True, "results": results})


@app.route("/", methods=["GET"])
def index():
    """Show available endpoints."""
    return jsonify({
        "service": "Laser HA Proxy",
        "endpoints": {
            "GET /health": "Health check",
            "GET|POST /laser/<device>/on": "Turn on device",
            "GET|POST /laser/<device>/off": "Turn off device",
            "GET|POST /laser/<device>/toggle": "Toggle device",
            "GET /laser/<device>/status": "Get device status",
            "GET /laser/status": "Get all device statuses",
            "GET|POST /laser/all/on": "Turn on water + air",
            "GET|POST /laser/all/off": "Turn off water + air",
        },
        "devices": list(LASER_ENTITIES.keys()),
    })


if __name__ == "__main__":
    log.info(f"Starting Laser HA Proxy on port {PORT}")
    log.info(f"HA URL: {HA_URL}")
    log.info(f"Token file: {TOKEN_FILE}")
    log.info(f"Devices: {list(LASER_ENTITIES.keys())}")

    app.run(host="0.0.0.0", port=PORT, debug=False)
