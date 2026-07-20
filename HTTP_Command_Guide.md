# HTTP Command Guide

The HTTP/GET command allows FluidNC G-code programs to make outgoing HTTP requests to external services, enabling integration with cloud APIs, local servers, IoT devices, and other networked systems.

## Basic Usage

### Simple Request

```gcode
$HTTP/GET=http://example.com/api
```

### Request with JSON Options

```gcode
$HTTP/GET=http://example.com/api{"method":"POST","timeout":3000,"body":"{\"key\":\"value\"}"}
```

### Command Substitution (Predefined Commands)

```gcode
$HTTP/GET=@fetch_temp
$HTTP/GET=@complex_cmd
```

## JSON Options

Customize HTTP requests with these options in a JSON object:

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `method` | string | GET | HTTP method: GET, POST, PUT, DELETE (automatically changes to POST if body is present) |
| `timeout` | integer | 5000 | Request timeout in milliseconds (max 10000) |
| `body` | string | "" | Request body content |
| `headers` | object | {} | Custom HTTP headers as key-value pairs |
| `extract` | object | {} | Maps GCode parameter names to JSON response keys for value extraction |
| `halt_on_error` | boolean | true | If true, HTTP errors halt GCode execution; if false, errors set `_HTTP_STATUS=0` and continue |

### Example with All Options

```gcode
$HTTP/GET=http://api.example.com/data{"method":"POST","timeout":8000,"body":"{\"sensor\":\"temp\"}","headers":{"Authorization":"Bearer token123","Content-Type":"application/json"},"extract":{"_temp":"temperature"},"halt_on_error":false}
```

## Token Substitution

Store long tokens in the settings file and reference them using `${token_name}` syntax. This avoids GCode line length limits and simplifies credential management.

### Settings File Format

Create `/localfs/http_settings.json`:

```json
{
  "tokens": {
    "ha_token": "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
    "api_key": "sk-abc123def456",
    "sensor_auth": "Basic dXNlcjpwYXNz"
  },
  "commands": {
    "fetch_temp": "http://sensor.local:8000/api/temperature",
    "log_metrics": "http://prometheus:9090/metrics{\"method\":\"POST\"}"
  }
}
```

### Using Tokens in G-Code

```gcode
; Simple token in URL
$HTTP/GET=http://ha.local:8123/api/states${ha_token}

; Token in headers
$HTTP/GET=http://api.example.com/data{"headers":{"Authorization":"${ha_token}"}}

; Token in body
$HTTP/GET=http://webhook.site/data{"method":"POST","body":"{\"token\":\"${api_key}\"}"}

; Multiple tokens
$HTTP/GET=http://api.example.com{"headers":{"Authorization":"${ha_token}","X-API-Key":"${api_key}"}}
```

> **Loading Tokens**: Tokens are loaded at system startup. To reload without rebooting:
> ```gcode
> $HTTP/Tokens/Reload
> ```

## Command Substitution

Store complete URLs or HTTP requests as named commands in the settings file and reference them with `@commandname`. This is ideal for complex requests that exceed G-code line limits.

### Defining Commands

In `/localfs/http_settings.json`:

```json
{
  "commands": {
    "fetch_temp": "http://sensor.local:8000/api/temperature",
    "post_metrics": "http://prometheus:9090/metrics{\"method\":\"POST\",\"timeout\":10000}",
    "complex_webhook": "http://webhook.example.com/notify{\"method\":\"POST\",\"headers\":{\"Authorization\":\"Bearer token\"},\"body\":\"{\\\"event\\\":\\\"job_complete\\\",\\\"timestamp\\\":\\\"${now}\\\"}\"}",
    "extract_data": "http://api.local/sensors{\"extract\":{\"_temp\":\"temperature\",\"_pressure\":\"pressure\"}}"
  }
}
```

### Using Commands in G-Code

```gcode
; Simple command reference
$HTTP/GET=@fetch_temp

; Commands can still have token substitution in the stored value
$HTTP/GET=@fancy_webhook

; Long URLs stored as commands avoid line length issues
$HTTP/GET=@extract_data
```

## GCode Parameters

After an HTTP request, these GCode parameters are automatically set:

| Parameter | Type | Description |
|-----------|------|-------------|
| `_HTTP_STATUS` | float | HTTP status code (0 if connection failed) |
| `_HTTP_RESPONSE_LEN` | float | Length of response body in bytes |
| Custom (from `extract`) | float | Any parameters defined in the `extract` option |

### Accessing Parameters in G-Code

```gcode
; Check if request succeeded
$HTTP/GET=http://api.example.com/status
o100 if [#<_HTTP_STATUS> EQ 200]
  (Request succeeded!)
o100 endif

; Use extracted numeric values
$HTTP/GET=http://sensor.local/read{"extract":{"_temp":"temperature","_humidity":"humidity"}}
#<temp_check> = [#<_temp> GT 45]
o200 if [#<temp_check> EQ 1]
  M0 (Temperature too high!)
o200 endif
```

## Response Data Extraction

Automatically extract numeric values from JSON responses and store them in GCode parameters.

### Settings

Define the `extract` option with parameter name → JSON key mappings:

```gcode
$HTTP/GET=http://sensor.local/data{"extract":{"_temperature":"temp","_humidity":"hum","_pressure":"press"}}

; Server returns: {"temp": 22.5, "hum": 55.0, "press": 1013.25}
; Parameters are now set:
; #<_temperature> = 22.5
; #<_humidity> = 55.0
; #<_pressure> = 1013.25
```

### Complex Example: Conditional Actions

```gcode
; Fetch sensor data with extraction
$HTTP/GET=http://sensors.local/room1{"extract":{"_temp":"temperature","_co2":"co2_ppm"}}

; Check temperature and CO2 levels
o100 if [#<_temp> GT 28]
  M118 Temperature high: #<_temp>C
o100 endif

o200 if [#<_co2> GT 1000]
  M118 CO2 levels high: #<_co2> ppm
o200 endif
```

## Complete Examples

### Architecture Note: Streaming Response Processing

The HTTP command uses a streaming JSON parser that processes responses character-by-character without buffering the entire response body. This design provides:

- **No artificial size limits** on responses (previously limited to 256 characters)
- **Lower memory footprint** by not storing complete response text
- **Real-time extraction** of values as response is received
- **Accurate byte counting** for `_HTTP_RESPONSE_LEN` parameter

Response data is parsed and extracted inline as it arrives from the server, eliminating the need for intermediate buffering while maintaining compatibility with incomplete or progressively-received JSON structures.

### Home Assistant Integration

```json
{
  "tokens": {
    "ha_token": "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9..."
  }
}
```

```gcode
; Get current temperature from Home Assistant
$HTTP/GET=http://homeassistant.local:8123/api/states/sensor.room_temperature{"headers":{"Authorization":"${ha_token}"},"extract":{"_room_temp":"state"}}

; Call a Home Assistant automation
$HTTP/GET=http://homeassistant.local:8123/api/webhook/job_complete{"method":"POST","headers":{"Authorization":"${ha_token}"},"body":"{\"status\":\"finished\"}"}
```

### Prometheus Metrics Submission

```json
{
  "commands": {
    "submit_metrics": "http://pushgateway:9091/metrics/job/cnc_machine{\"method\":\"POST\",\"body\":\"machine_runtime_seconds 3600\"}",
    "fetch_metrics": "http://prometheus:9090/api/v1/instant?query=up"
  }
}
```

```gcode
; Submit job completion metric
$HTTP/GET=@submit_metrics

; Fetch current system uptime
$HTTP/GET=@fetch_metrics{"extract":{"_uptime":"value"}}
```

### Webhook Logging

```json
{
  "commands": {
    "log_job": "http://webhook.site/logs{\"method\":\"POST\",\"headers\":{\"Content-Type\":\"application/json\"},\"body\":\"{\\\"message\\\":\\\"Job #1 completed\\\",\\\"date\\\":\\\"2025-03-07\\\"}\"}"
  }
}
```

```gcode
; Log job completion
$HTTP/GET=@log_job

; Check response
o300 if [#<_HTTP_STATUS> EQ 200]
  M118 Job logged successfully
o300 endif
```

## State Restrictions

HTTP requests are blocked during certain machine states to prevent unsafe behavior:

- **Homing** - Cannot make requests during homing cycle
- **Jogging** - Blocked during manual jog operations
- **Safety Door** - Blocked when safety door is open
- **Sleep** - Blocked when machine is in sleep mode

Requests during **Cycle** state will log a warning about potential buffer underrun but are allowed.

## Important Limitations

- **Blocks GCode Processing**: HTTP requests block GCode command processing during the request (stepper motion is not blocked)
- **Maximum Timeout**: 10 seconds (10000 ms)
- **WiFi Required**: Only works when WiFi is connected
- **HTTPS Certificates**: Not validated (self-signed certificates are accepted)
- **Numeric Extraction Only**: Only numeric JSON values can be extracted into GCode parameters
- **Error Handling**: String values in JSON responses cannot be stored in parameters (which are floats)

## Settings File Reference

Complete `/localfs/http_settings.json` structure:

```json
{
  "tokens": {
    "token_name": "token_value",
    "api_key": "sk-xxxxx",
    "auth_header": "Bearer xxxx"
  },
  "commands": {
    "command_name": "http://full/url/or/url{json_options}",
    "fetch_data": "http://api.local/endpoint",
    "webhook": "http://webhook/notify{\"method\":\"POST\",\"body\":\"...\"}"
  }
}
```

- `tokens`: Object with arbitrary key-value pairs for token storage
- `commands`: Object with arbitrary key-value pairs for command storage
- Both keys are optional
- Both are reloaded from disk at startup and via `$HTTP/Tokens/Reload`

## Troubleshooting

### Unknown Command Error
```
HTTP: Unknown command '@commandname'
```
**Solution**: Check that the command name is spelled correctly in both the G-code and `/localfs/http_settings.json`.

### Connection Refused
```
HTTP: Response timeout
```
**Solution**: Verify the server is running and reachable on the specified address and port. Check WiFi connection status.

### Status Code Errors (4xx, 5xx)
```
HTTP: Server returned 404
```
**Solution**: Check the URL is correct and the server endpoint exists. Check headers and authentication tokens are valid.

### Response Too Large
The response body is truncated to 256 characters. For larger responses:
- Use the `extract` option to pull only needed values
- Configure server to return minimal response data

### Parameter Extraction Not Working
**Solution**: 
- Ensure response is valid JSON
- Verify key names match exactly (case-sensitive)
- Ensure values are numeric (not quoted strings)
- Check `_HTTP_STATUS` is 200-299 for successful extraction

## Related Commands

- `$HTTP/Tokens/Reload` - Reload tokens and commands from settings file without reboot
- `$HTTP/GET=url` - Execute HTTP request
- `$HTTP/GET=@commandname` - Execute predefined command
- `$HTTP/GET=url{json_options}` - Execute request with custom options

## See Also

- [GCode Parameters Reference](/reference/gcode-parameters)
- [Webhook Documentation](/integrations/webhooks)
- [WiFi Configuration](/setup/wifi-setup)
