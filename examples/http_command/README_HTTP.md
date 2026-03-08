# HttpCommand Examples

This directory contains example files demonstrating the HttpCommand feature in FluidNC, which allows G-code programs to make outgoing HTTP requests to integrate with cloud APIs, local servers, IoT devices, and other networked systems.

The command name, which you can issue from any GCode context, is **$HTTP/Command**, or **$HC** for short.

## Files

### `http_settings.json`
A comprehensive settings file showing all HttpCommand features:

- **Tokens** - Securely store long credentials, API keys, and bearer tokens
  - `ha_token` - Home Assistant authentication token
  - `prometheus_api_key` - Prometheus/metrics API key
  - `influx_token` - InfluxDB authentication token
  - `webhook_api_key` - Webhook service API key
  - Plus username/password pairs and base URLs

- **Commands** - Predefined HTTP requests referenced from G-code
  - Simple queries: `fetch_temperature`, `fetch_humidity`
  - Multi-value extraction: `fetch_all_sensors` (extracts temp, humidity, pressure)
  - Smart home: `ha_get_room_temp`, `ha_call_automation`
  - Monitoring: `check_system_health`, `post_job_metrics`
  - Databases: `post_to_influxdb`, `query_prometheus`
  - Webhooks: `webhook_log_start`, `webhook_log_complete`
  - Job queuing: `get_next_job`
  - Tool management: `fetch_tool_offsets`
  - Different HTTP methods: PUT (`put_job_status`), DELETE (`delete_old_logs`)

### `http_example.ngc`
A G-code example program demonstrating how to use the commands and tokens from `http_settings.json`:

- **Simple command usage** - Basic command substitution with `@command_name`
- **Token substitution** - Using `${token_name}` in API calls
- **Multiple extractions** - Single request extracting multiple JSON fields
- **Error handling** - Checking HTTP status codes and taking actions
- **Data validation** - Conditional logic based on fetched sensor values
- **Metrics submission** - Posting job statistics to monitoring systems
- **Different HTTP methods** - GET, POST, PUT, DELETE examples
- **Timeout handling** - Requests with custom timeout values

## How to Use

### Setup

1. Copy `http_settings.json` to `/localfs/http_settings.json` on your FluidNC device/SD card
2. Update the URLs and tokens with your actual API endpoints and credentials
3. Use `http_example.ngc` as a reference for your own G-code programs

### Editing http_settings.json

Replace placeholder values:
- `http://homeassistant.local:8123` → your Home Assistant URL
- `http://sensor.local:8000` → your sensor server URL
- `Bearer eyJ...` → your actual Home Assistant token
- `sk-abc123...` → your actual API keys
- `webhook.site/abc123xyz` → your actual webhook URL

### Tokens

Tokens are useful for:
- Storing long API keys (avoids G-code line length limits)
- Reusing credentials across multiple commands
- Easy updates (change token once, affects all uses)

Reference tokens in URLs/headers/body using `${token_name}`:
```gcode
$HTTP/Command=http://api.example.com/data{"headers":{"Authorization":"${ha_token}"}}
```

### Commands

Commands are useful for:
- Complex multi-line requests with headers and body
- Requests that exceed G-code line length limits
- Reusable request templates

Reference commands with `@command_name`:
```gcode
$HTTP/Command=@fetch_temperature
$HTTP/Command=@ha_call_automation
```

### Data Extraction

Extract numeric JSON values into GCode parameters:
```gcode
$HTTP/Command=@fetch_all_sensors
(After request completes:)
(#<_temperature> = fetched temperature value)
(#<_humidity> = fetched humidity value)
(#<_pressure> = fetched pressure value)
```

Use extracted values in conditional logic:
```gcode
o100 if [#<_temperature> GT 35]
  M118 WARNING: Temperature too high!
  M0
o100 endif
```

## Real-World Scenarios

### Home Automation
```gcode
(Get temperature from Home Assistant)
$HTTP/Command=@ha_get_room_temp
(Trigger automation when job completes)
$HTTP/Command=@ha_call_automation
```

### Monitoring & Metrics
```gcode
(Check system health before starting)
$HTTP/Command=@check_system_health
(Submit job metrics when done)
$HTTP/Command=@post_job_metrics
(Post data to InfluxDB for long-term trending)
$HTTP/Command=@post_to_influxdb
```

### Job Queuing
```gcode
(Get next job from queue)
$HTTP/Command=@get_next_job
(Use extracted job_id and part_count)
#<job_id> = #<_job_id>
#<parts> = #<_part_count>
(... process parts ...)
```

### Webhook Logging
```gcode
(Log job start)
$HTTP/Command=@webhook_log_start
(... perform work ...)
(Log job completion)
$HTTP/Command=@webhook_log_complete
```

## Features Demonstrated

✓ **Token substitution** - `${token_name}` in URLs, headers, body  
✓ **Command substitution** - `@command_name` references  
✓ **Multiple values extraction** - Extract many JSON fields in one request  
✓ **Custom headers** - Authorization, Content-Type, custom headers  
✓ **Different HTTP methods** - GET, POST, PUT, DELETE  
✓ **Request body** - Send JSON data in requests  
✓ **Timeout control** - Customize request timeout  
✓ **Error handling** - Check response status codes  
✓ **Conditional logic** - Branch based on HTTP responses  
✓ **Metrics submission** - Send data to monitoring systems  
✓ **APIs integration** - Home Assistant, Prometheus, InfluxDB, webhooks  

## Testing with http_command_server.py

The included `http_command_server.py` provides a test HTTP server that simulates various external APIs and sensor endpoints. Use this to test HttpCommand functionality without needing real external services.

### Starting the Test Server

```bash
# Start on default port 8000
python3 http_command_server.py

# Start on a specific port
python3 http_command_server.py --port 9000

# With verbose output (shows all requests)
python3 http_command_server.py --port 8000 --verbose
```

The server will start and listen for incoming requests. Output:
```
Server running on http://localhost:8000
Press Ctrl+C to stop
```

### Available Test Endpoints

The test server provides these endpoints:

| Endpoint | Method | Response | Use Case |
|----------|--------|----------|----------|
| `/api/temperature` | GET | Single sensor value | Test single value extraction |
| `/api/humidity` | GET | Single sensor value | Test single value extraction |
| `/api/read` | GET | Multiple values (temp, humidity, pressure) | Test multi-value extraction |
| `/api/health` | GET | Health check status | Test error handling |
| `/api/states/{sensor}` | GET | Home Assistant format | Test HA integration |
| `/api/jobs/next` | GET | Job queue data | Test job queuing commands |
| `/api/webhook/{name}` | POST | Webhook confirmation | Test webhook logging |
| `/metrics/*` | POST/PUT | Metrics acceptance | Test metrics submission |

### Quick Testing with curl

Before running G-code, verify endpoints work with curl:

```bash
# Test single value extraction
curl http://localhost:8000/api/temperature

# Response: {"temperature": 22.5, "unit": "celsius", "timestamp": "..."}

# Test multi-value extraction
curl http://localhost:8000/api/read

# Response: {"temp": 23.1, "humidity": 51.2, "pressure": 1013.45, ...}

# Test POST webhook
curl -X POST http://localhost:8000/api/webhook/job_complete \
  -H "Content-Type: application/json" \
  -d '{"status":"finished","duration":3600}'

# Test job queue
curl http://localhost:8000/api/jobs/next
```

### Testing with FluidNC G-code

#### 1. Update http_settings.json

Modify the URLs to point to your test server:

```json
{
  "commands": {
    "fetch_temperature": "http://localhost:8000/api/temperature",
    "fetch_all_sensors": "http://localhost:8000/api/read",
    "get_next_job": "http://localhost:8000/api/jobs/next",
    "webhook_log_complete": "http://localhost:8000/api/webhook/job_complete"
  }
}
```

#### 2. Copy settings to device

```bash
# For local testing (ESP32 with SD card)
cp http_settings.json /mnt/sd_card/localfs/

# Or via WiFi with FluidNC command interface
# $HTTP/Settings/Load would reload after updating /localfs/http_settings.json
```

#### 3. Run test G-code

Using `http_example.ngc` or your own test program:

```gcode
; Simple extraction test
$HTTP/Command=@fetch_temperature
#<temp> = #<_temperature>
M118 Current temperature: #<_temperature>

; Multi-value extraction
$HTTP/Command=@fetch_all_sensors
M118 Temp: #<_temp> Humidity: #<_humidity> Pressure: #<_pressure>

; Conditional logic
o100 if [#<_temperature> GT 30]
  M118 WARNING: Temperature too high
  M0
o100 endif

; Webhook logging
$HTTP/Command=@webhook_log_complete
```

### Monitoring Test Requests

The test server logs all HTTP requests. Look for output like:

```
GET /api/temperature - 200 OK
POST /api/webhook/job_complete - 200 OK
PUT /metrics/spindle - 200 OK
```

### Simulating Different Responses

To test error handling, you can:

1. **Stop the server** - Causes connection timeout (tests `_HTTP_STATUS = 0`)
2. **Change request parameters** - Test different response paths
3. **Modify http_settings.json** - Point to non-existent endpoints (tests 404 errors)

Example error handling in G-code:

```gcode
$HTTP/Command=http://localhost:9999/api/temperature
(Request will timeout/fail since port 9999 has no server)
#<_status> = #<_HTTP_STATUS>

o100 if [#<_status> EQ 0]
  M118 HTTP Request failed!
  (Take some action like retry or skip operation)
o100 endif
```

### Network Testing

To test from a different machine on the network:

```bash
# Start server listening on all interfaces
python3 http_command_server.py --host 0.0.0.0 --port 8000

# From another machine, test with:
curl http://<server-ip>:8000/api/temperature

# In FluidNC, update commands to use the server IP:
# $HTTP/Command=http://<server-ip>:8000/api/read
```

## Important Notes

- Settings file is loaded at startup from `/localfs/http_settings.json`
- Reload without reboot using: `$HTTP/Settings/Load`
- Timestamps in the example are static (could be parameterized in real usage)
- Replace example URLs with your actual server addresses
- Keep API tokens and credentials secure (don't share your settings file)
- Requests timeout after 10 seconds maximum
- HTTP requests block G-code execution (stepper motion is not blocked)
- Numeric extraction only (string values cannot be stored in GCode parameters)

## See Also

- [HTTP_Command_Guide.md](../HTTP_Command_Guide.md) - Complete documentation
- [Other example configs](.) - Additional FluidNC configuration examples
