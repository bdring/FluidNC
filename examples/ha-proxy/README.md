# Home Assistant Proxy for FluidNC HTTP Command

A lightweight proxy server that enables FluidNC's `$HTTP` command to control Home Assistant devices with short, simple URLs.

## Why Use a Proxy?

FluidNC has a 255-character line limit for GCode commands. Home Assistant requires long JWT tokens for authentication, which often exceed this limit when combined with the URL and request body.

The proxy solves this by:
- Handling authentication server-side
- Providing short, memorable endpoints
- Enabling simple GCode commands like `$HTTP=http://proxy:5050/laser/water/on`

## Quick Start

1. Copy this directory to your Home Assistant server
2. Configure the environment variables in `.env`
3. Install dependencies: `pip install -r requirements.txt`
4. Run: `python ha_proxy.py`
5. (Optional) Install as systemd service: `sudo cp ha-proxy.service /etc/systemd/system/`

## Configuration

Create a `.env` file with:
```
HA_HOST=http://localhost:8123
HA_TOKEN=your_long_lived_access_token
PROXY_PORT=5050
```

## Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/laser/water/on` | POST | Turn on water cooling |
| `/laser/water/off` | POST | Turn off water cooling |
| `/laser/air/on` | POST | Turn on air assist |
| `/laser/air/off` | POST | Turn off air assist |
| `/laser/status` | GET | Get status of all laser switches |
| `/health` | GET | Health check |

## GCode Usage

```gcode
; Turn on water cooling (critical - halt on failure)
$HTTP=http://nara:5050/laser/water/on

; Turn on air assist (non-critical - continue on failure)
$HTTP=http://nara:5050/laser/air/on{"fail":false}

; Check status
$HTTP=http://nara:5050/laser/status
```

## Customization

Edit `ha_proxy.py` to add your own endpoints for different Home Assistant entities.

## Security Note

This proxy should only be accessible on your local network. Do not expose it to the internet.
