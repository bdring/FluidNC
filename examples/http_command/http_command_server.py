#!/usr/bin/env python3
"""
HTTP server supporting HttpCommand examples.

Responds to various endpoints to demonstrate HttpCommand features:
- /api/temperature - Single temperature value
- /api/humidity - Single humidity value
- /api/read - Multiple sensors (temperature, humidity, pressure)
- /api/states/* - Home Assistant-compatible state endpoints
- /api/webhook/* - Webhook endpoints accepting POST
- /api/health - Health check with CPU and memory info
- /api/jobs/next - Job queue endpoint with ID and quantity
- /api/v1/instant - Prometheus query endpoint
- /metrics/* - Metrics endpoints

Usage:
    python3 http_status_server.py [--port 8000]

Then access:
    curl http://localhost:8000/api/temperature
    curl http://localhost:8000/api/read
    curl -X POST http://localhost:8000/api/webhook/cnc_job_complete -H "Content-Type: application/json" -d '{"status":"finished"}'
"""

import json
import sys
import argparse
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import random
from datetime import datetime

# Optional: psutil for real system metrics
try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False


class StatusHandler(BaseHTTPRequestHandler):
    """HTTP request handler supporting multiple endpoints"""
    
    def do_GET(self):
        """Handle GET requests"""
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        # Route to different handlers based on path
        if path == '/api/status':
            self.handle_status()
        elif path == '/api/temperature':
            self.handle_temperature()
        elif path == '/api/humidity':
            self.handle_humidity()
        elif path == '/api/read':
            self.handle_read()
        elif path == '/api/health':
            self.handle_health_detailed()
        elif path.startswith('/api/states/'):
            self.handle_home_assistant_state(path)
        elif path == '/api/jobs/next':
            self.handle_jobs_next()
        elif path == '/api/v1/query':
            self.handle_prometheus_query(parsed_path.query)
        elif path.startswith('/metrics/'):
            self.handle_metrics()
        elif path == '/health':
            self.handle_health()
        elif path == '/':
            self.handle_root()
        else:
            self.send_error(404, "Not Found")
    
    def do_POST(self):
        """Handle POST requests"""
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        # Read request body
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length else ""
        
        if path.startswith('/api/webhook/'):
            self.handle_webhook(path, body)
        elif path == '/metrics/job/cnc_machine/instance/spindle1':
            self.handle_prometheus_push(body)
        elif path.startswith('/api/v2/write'):
            self.handle_influxdb_write(body)
        else:
            self.send_error(404, "Not Found")
    
    def do_PUT(self):
        """Handle PUT requests"""
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length).decode('utf-8') if content_length else ""
        
        if path.startswith('/api/jobs/'):
            self.handle_job_status_update(path, body)
        else:
            self.send_error(404, "Not Found")
    
    def do_DELETE(self):
        """Handle DELETE requests"""
        parsed_path = urlparse(self.path)
        path = parsed_path.path
        
        if path.startswith('/api/v1/cleanup'):
            self.handle_cleanup()
        else:
            self.send_error(404, "Not Found")
    
    def handle_status(self):
        """Respond to /api/status with temperature and humidity"""
        response_data = {
            "status": "ok",
            "timestamp": datetime.now().isoformat(),
            "temperature": round(20.5 + random.uniform(-2, 2), 1),
            "humidity": round(50 + random.uniform(-10, 10), 1),
            "uptime_seconds": 12345
        }
        self.send_json_response(response_data)
    
    def handle_temperature(self):
        """Respond to /api/temperature with just temperature"""
        response_data = {
            "temperature": round(22.0 + random.uniform(-3, 3), 1),
            "unit": "celsius",
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data)
    
    def handle_humidity(self):
        """Respond to /api/humidity with just humidity"""
        response_data = {
            "humidity": round(55 + random.uniform(-15, 15), 1),
            "unit": "percent",
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data)
    
    def handle_read(self):
        """Respond to /api/read with multiple sensor values"""
        response_data = {
            "temp": round(23.0 + random.uniform(-2, 2), 1),
            "humidity": round(52 + random.uniform(-8, 8), 1),
            "pressure": round(1013.25 + random.uniform(-5, 5), 2),
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data)
    
    def handle_health(self):
        """Respond to /health with simple health check"""
        response_data = {
            "status": "healthy",
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data)
    
    def handle_health_detailed(self):
        """Respond to /api/health with CPU and memory stats"""
        if HAS_PSUTIL:
            try:
                cpu_percent = psutil.cpu_percent(interval=0.1)
                memory = psutil.virtual_memory()
                memory_mb = memory.available / (1024 * 1024)
            except:
                cpu_percent = random.uniform(10, 70)
                memory_mb = random.uniform(500, 4000)
        else:
            cpu_percent = random.uniform(10, 70)
            memory_mb = random.uniform(500, 4000)
        
        response_data = {
            "status": "healthy",
            "timestamp": datetime.now().isoformat(),
            "cpu_percent": round(cpu_percent, 1),
            "memory_mb": round(memory_mb, 1)
        }
        self.send_json_response(response_data)
    
    def handle_home_assistant_state(self, path):
        """Respond to /api/states/sensor.* endpoints (Home Assistant format)"""
        # Extract sensor name from path
        sensor_name = path.split('/')[-1]
        
        # Generate appropriate response based on sensor type
        if 'temperature' in sensor_name:
            state_value = round(21.5 + random.uniform(-3, 3), 1)
        elif 'humidity' in sensor_name:
            state_value = round(54 + random.uniform(-10, 10), 1)
        elif 'pressure' in sensor_name:
            state_value = round(1013.0 + random.uniform(-5, 5), 1)
        else:
            state_value = round(random.uniform(0, 100), 1)
        
        response_data = {
            "entity_id": f"sensor.{sensor_name}",
            "state": str(state_value),
            "attributes": {
                "unit_of_measurement": "°C" if 'temperature' in sensor_name else "%",
                "friendly_name": sensor_name.replace('_', ' ').title()
            },
            "last_updated": datetime.now().isoformat(),
            "last_changed": datetime.now().isoformat()
        }
        self.send_json_response(response_data)
    
    def handle_jobs_next(self):
        """Respond to /api/jobs/next with next job details"""
        response_data = {
            "id": random.randint(1000, 9999),
            "quantity": random.randint(5, 20),
            "material": "aluminum",
            "estimated_time_minutes": random.randint(10, 120),
            "priority": "normal"
        }
        self.send_json_response(response_data)
    
    def handle_prometheus_query(self, query_string):
        """Respond to /api/v1/query Prometheus endpoint"""
        # Parse query parameter
        params = parse_qs(query_string)
        query = params.get('query', [''])[0]
        
        # Simulate Prometheus response
        if 'machine_uptime_seconds' in query:
            value = str(random.randint(3600, 86400))
        else:
            value = str(random.uniform(10, 100))
        
        response_data = {
            "status": "success",
            "data": {
                "resultType": "instant",
                "result": [
                    {
                        "metric": {"__name__": query},
                        "value": [int(datetime.now().timestamp()), value]
                    }
                ]
            }
        }
        self.send_json_response(response_data)
    
    def handle_webhook(self, path, body):
        """Handle webhook POST requests"""
        webhook_name = path.split('/')[-1]
        
        # Log the webhook call
        print(f"Webhook '{webhook_name}' received")
        if body:
            try:
                data = json.loads(body)
                print(f"  Data: {json.dumps(data, indent=2)}")
            except:
                print(f"  Raw body: {body}")
        
        # Return success response
        response_data = {
            "status": "received",
            "webhook": webhook_name,
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data, 200)
    
    def handle_prometheus_push(self, body):
        """Handle Prometheus pushgateway POST requests"""
        print(f"Prometheus metrics received:")
        print(f"  {body}")
        
        response_data = {"status": "accepted"}
        self.send_json_response(response_data, 202)
    
    def handle_influxdb_write(self, body):
        """Handle InfluxDB line protocol write requests"""
        print(f"InfluxDB write received:")
        print(f"  {body}")
        
        # Return 204 No Content for successful write
        self.send_response(204)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
    
    def handle_job_status_update(self, path, body):
        """Handle PUT requests to update job status"""
        job_id = path.split('/')[-1]
        print(f"Job {job_id} status update: {body}")
        
        response_data = {
            "job_id": job_id,
            "status": "updated",
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data, 200)
    
    def handle_cleanup(self):
        """Handle DELETE request for cleanup"""
        print(f"Cleanup action triggered")
        
        # Return 204 No Content for successful delete
        self.send_response(204)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
    
    def handle_metrics(self):
        """Handle /metrics/* endpoints"""
        response_data = {
            "status": "metrics_received",
            "timestamp": datetime.now().isoformat()
        }
        self.send_json_response(response_data, 200)
    
    def handle_root(self):
        """Respond to / with API documentation"""
        response_data = {
            "api": "HttpCommand Example Server",
            "version": "2.0",
            "endpoints": {
                "GET": {
                    "/api/status": "Status with temperature, humidity, uptime",
                    "/api/temperature": "Single temperature value",
                    "/api/humidity": "Single humidity value",
                    "/api/read": "Multiple sensors (temp, humidity, pressure)",
                    "/api/health": "Health check with CPU and memory",
                    "/api/states/sensor.*": "Home Assistant sensor state",
                    "/api/jobs/next": "Get next job from queue",
                    "/api/v1/query": "Prometheus instant query",
                    "/health": "Simple health check"
                },
                "POST": {
                    "/api/webhook/*": "Receive webhook notifications",
                    "/metrics/job/*/instance/*": "Prometheus pushgateway metrics",
                    "/api/v2/write": "InfluxDB line protocol write"
                },
                "PUT": {
                    "/api/jobs/*": "Update job status"
                },
                "DELETE": {
                    "/api/v1/cleanup": "Cleanup old logs"
                }
            }
        }
        self.send_json_response(response_data)
    
    def send_json_response(self, data, status_code=200):
        """Send a JSON response"""
        json_response = json.dumps(data, indent=2)
        json_bytes = json_response.encode('utf-8')
        
        self.send_response(status_code)
        self.send_header('Content-type', 'application/json')
        self.send_header('Content-Length', len(json_bytes))
        self.send_header('Access-Control-Allow-Origin', '*')  # Allow CORS
        self.end_headers()
        self.wfile.write(json_bytes)
    
    def log_message(self, format, *args):
        """Override logging to show timestamp"""
        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {format % args}")


def main():
    parser = argparse.ArgumentParser(
        description='HTTP server supporting HttpCommand examples and demonstrations'
    )
    parser.add_argument(
        '--port',
        type=int,
        default=8000,
        help='Port to listen on (default: 8000)'
    )
    parser.add_argument(
        '--host',
        type=str,
        default='0.0.0.0',
        help='Host to bind to (default: 0.0.0.0 - all public interfaces)'
    )
    
    args = parser.parse_args()
    
    server_address = (args.host, args.port)
    httpd = HTTPServer(server_address, StatusHandler)
    
    # Display server info
    display_host = args.host if args.host != '0.0.0.0' else 'all interfaces'
    print(f"Starting HttpCommand Example Server on port {args.port} (binding to {display_host})")
    print(f"\nSupported Endpoints:")
    print(f"GET Endpoints:")
    print(f"  http://localhost:{args.port}/api/status              - Status with all fields")
    print(f"  http://localhost:{args.port}/api/temperature         - Single temperature")
    print(f"  http://localhost:{args.port}/api/humidity            - Single humidity")
    print(f"  http://localhost:{args.port}/api/read                - Multiple sensors (temp, humidity, pressure)")
    print(f"  http://localhost:{args.port}/api/health              - Health check with CPU/memory")
    print(f"  http://localhost:{args.port}/api/states/sensor.workshop_temperature")
    print(f"  http://localhost:{args.port}/api/jobs/next           - Next job from queue")
    print(f"  http://localhost:{args.port}/api/v1/query?query=...  - Prometheus query")
    print(f"  http://localhost:{args.port}/health                  - Simple health check")
    print(f"\nPOST Endpoints:")
    print(f"  http://localhost:{args.port}/api/webhook/cnc_job_complete")
    print(f"  http://localhost:{args.port}/metrics/job/cnc_machine/instance/spindle1")
    print(f"  http://localhost:{args.port}/api/v2/write")
    print(f"\nPUT Endpoints:")
    print(f"  http://localhost:{args.port}/api/jobs/123            - Update job status")
    print(f"\nDELETE Endpoints:")
    print(f"  http://localhost:{args.port}/api/v1/cleanup          - Cleanup old logs")
    print(f"\nExample queries:")
    print(f"  curl http://localhost:{args.port}/api/temperature")
    print(f"  curl http://localhost:{args.port}/api/read")
    print(f"  curl -X POST http://localhost:{args.port}/api/webhook/cnc_job_complete -H 'Content-Type: application/json' -d '{{\"status\":\"finished\"}}'")
    print(f"\nPress Ctrl+C to stop")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down...")
        httpd.shutdown()
        sys.exit(0)


if __name__ == '__main__':
    main()
