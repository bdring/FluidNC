# FluidNC HTTP Command Feature Overview

This document describes the `$HTTP` command feature for FluidNC, which allows GCode programs to make outgoing HTTP requests to external services (e.g., Home Assistant, webhooks, REST APIs).

## Use Case

Enable CNC machines to communicate with external systems during job execution:

```gcode
; Turn on water cooling via Home Assistant
$HTTP=https://homeassistant:8123/api/services/switch/turn_on{"headers":{"Authorization":"Bearer TOKEN"},"body":"{\"entity_id\":\"switch.laser_water\"}"}
G4 P1000 ; Wait for request to complete
```

## State Machine & Operational Modes

FluidNC operates with the following states (defined in `FluidNC/src/State.h`):

| State | Description | $HTTP Allowed? |
|-------|-------------|----------------|
| `Idle` | Ready for commands | Yes |
| `Alarm` | Alarm state, locked out | Yes (non-motion) |
| `CheckMode` | G-code check mode | Yes (no actual request) |
| `Homing` | Performing homing cycle | No |
| `Cycle` | Motion is executing | With caveats |
| `Hold` | Decelerating to hold | Yes |
| `Jog` | Jogging mode | No |
| `SafetyDoor` | Door ajar | No |
| `Sleep` | Sleep state | No |
| `ConfigAlarm` / `Critical` | Fatal states | No |

## Threading Model

FluidNC runs multiple FreeRTOS tasks:

| Task | Purpose | Timing Sensitivity |
|------|---------|-------------------|
| `polling_loop` | Input polling, reads lines | Low |
| `output_loop` | Message output queue | Low |
| `protocol_main_loop` | GCode execution | Medium |
| **Stepper ISR** | Step pulse generation | **Critical** |

**Key insight**: The protocol loop processes GCode lines synchronously. An HTTP request will block further line processing but will NOT affect ongoing stepper motion (the ISR runs independently).

## Timing Constraints

| Scenario | Safe? | Reason |
|----------|-------|--------|
| Before/after job | Yes | System idle |
| During `G4` dwell | Risky | Dwell timing affected |
| Between motion blocks | No | May cause motion stutter |
| When buffer has queued moves | Maybe | Motion continues while blocked |
| Spindle on, not moving | Yes | No timing impact |

**Recommendation**: Use `$HTTP` only when timing is not critical:
- At job start/end (before first move, after last move)
- During deliberate pauses (M0/M1)
- When motion planner buffer is full (moves will continue)

## GCode Parameter Integration

FluidNC supports parameters that can store HTTP response data:

**Automatic parameters** (set after each request):
- `#<_HTTP_STATUS>` - HTTP status code from last request
- `#<_HTTP_RESPONSE_LEN>` - Length of response body

**Extracted parameters** (user-defined via `extract` option):
Use the `extract` JSON option to parse numeric values from JSON responses and store them in named GCode parameters. The `extract` object maps GCode parameter names to JSON keys.

**Usage example - check status**:
```gcode
$HTTP=https://api.example.com/status
; Check if request succeeded (status 200)
```

**Usage example - extract sensor data**:
```gcode
; Server returns: {"temperature": 25.5, "humidity": 65.2}
$HTTP=http://sensor-server/api{"extract":{"_temp":"temperature","_humid":"humidity"}}
; Now #<_temp> = 25.5 and #<_humid> = 65.2
```

**Note**: Only numeric JSON values (integers and floats) can be extracted. String values cannot be stored in GCode parameters.

## Command Syntax

```
$HTTP=url
$HTTP=url{json_options}
```

**Parameters**:
- `url` - Full URL including protocol (http:// or https://)
- `json_options` - Optional JSON object with fields:
  - `headers` - Object of HTTP headers
  - `body` - Request body (string)
  - `method` - HTTP method (default: POST if body present, GET otherwise)
  - `timeout` - Request timeout in milliseconds (default: 5000, max: 10000)
  - `extract` - Object mapping GCode parameter names to JSON keys to extract from response

**Examples**:

Simple GET request:
```
$HTTP=https://example.com/api/status
```

POST with JSON body:
```
$HTTP=https://example.com/api/action{"method":"POST","body":"{\"action\":\"start\"}"}
```

With custom headers:
```
$HTTP=https://example.com/api{"headers":{"Authorization":"Bearer token123"},"body":"{\"key\":\"value\"}"}
```

Extract values from JSON response:
```
; Fetch sensor data and extract values into GCode parameters
$HTTP=http://192.168.1.100:5000/sensors{"extract":{"_temp":"temperature","_humid":"humidity"}}
; Parameters _temp and _humid now contain the extracted values
; Use them in GCode expressions, e.g., conditional logic based on temperature
```

## Error Handling

The command returns FluidNC error codes:

| Error | Meaning |
|-------|---------|
| `Error::Ok` | Request successful (2xx status) |
| `Error::FsFailedOpenFile` | Connection failed |
| `Error::InvalidValue` | Malformed URL or JSON |
| `Error::InvalidStatement` | Bad command format |

HTTP status codes are stored in `#<_HTTP_STATUS>` regardless of success/failure.

## Limitations

1. **Blocking**: Requests block GCode processing (not stepper motion)
2. **Timeout**: Maximum 10 seconds per request
3. **Response size**: Response body truncated to 256 characters
4. **WiFi required**: Only works when WiFi is connected in STA mode
5. **No streaming**: Cannot stream large responses
6. **HTTPS recommended**: Both HTTP and HTTPS work, but HTTPS certificates are not validated (insecure mode)
