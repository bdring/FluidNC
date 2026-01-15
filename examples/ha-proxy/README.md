# Home Assistant Proxy for FluidNC HTTP Command

A lightweight proxy server that enables FluidNC's `$HTTP` command to control Home Assistant devices with short, simple URLs.

## Why Use a Proxy?

FluidNC has a 255-character line limit for GCode commands. Home Assistant requires long JWT tokens for authentication, which often exceed this limit when combined with the URL and request body.

The proxy solves this by:
- Handling authentication server-side
- Providing short, memorable endpoints
- Enabling simple GCode commands like `$HTTP=http://proxy:5050/laser/water/on`

## Quick Start

1. Copy this directory to a server on your network
2. Create `.ha_token` with your Home Assistant long-lived access token
3. Install dependencies: `pip install -r requirements.txt`
4. Edit `ha_proxy.py` to customize `LASER_ENTITIES` for your devices
5. Run: `python ha_proxy.py`

## Configuration

Set environment variables or edit the defaults in `ha_proxy.py`:

| Variable | Default | Description |
|----------|---------|-------------|
| `HA_URL` | `http://localhost:8123` | Home Assistant URL |
| `HA_TOKEN_FILE` | `.ha_token` | Path to token file |
| `PORT` | `5050` | Proxy listen port |

**Important:** Use IP addresses instead of hostnames in GCode - ESP32 DNS resolution is unreliable.

## Installation as Service

```bash
# Edit ha-proxy.service with your paths and user
sudo cp ha-proxy.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ha-proxy
sudo systemctl start ha-proxy
```

## Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/laser/<device>/on` | GET/POST | Turn on device |
| `/laser/<device>/off` | GET/POST | Turn off device |
| `/laser/<device>/toggle` | GET/POST | Toggle device |
| `/laser/<device>/status` | GET | Get device status |
| `/laser/all/on` | GET/POST | Turn on water + air |
| `/laser/all/off` | GET/POST | Turn off water + air |
| `/laser/status` | GET | Get all device statuses |
| `/health` | GET | Health check |

Default devices: `water`, `air`, `vent`, `power` (customize in `LASER_ENTITIES`)

## GCode Usage

```gcode
; Turn on water cooling (critical - halt on failure)
$HTTP=http://192.168.1.100:5050/laser/water/on

; Turn on air assist (non-critical - continue on failure)
$HTTP=http://192.168.1.100:5050/laser/air/on{"halt_on_error":false}

; Turn on all laser support systems
$HTTP=http://192.168.1.100:5050/laser/all/on

; Check status
$HTTP=http://192.168.1.100:5050/laser/status

; Turn off everything at end of job
$HTTP=http://192.168.1.100:5050/laser/all/off
```

Replace `192.168.1.100` with the IP address of your proxy server.

## Customization

Edit `ha_proxy.py` to add your own endpoints for different Home Assistant entities.

## Security Note

This proxy should only be accessible on your local network. Do not expose it to the internet.
