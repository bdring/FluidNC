// Copyright (c) 2025 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// HTTP Command for FluidNC
// Allows GCode programs to make outgoing HTTP requests to external services.
//
// Usage: $HTTP=url or $HTTP=url{json_options}
//
// Examples:
//   $HTTP=http://example.com/api
//   $HTTP=http://example.com/api{"method":"POST","body":"{\"key\":\"value\"}"}
//
// Limitations:
//   - Blocks GCode processing (not stepper motion) during request
//   - Maximum timeout: 10 seconds
//   - Response body truncated to 256 characters
//   - Only works when WiFi is connected
//   - HTTPS certificates are not validated

#include "HttpCommand.h"

#include "../System.h"
#include "../Protocol.h"
#include "../Parameters.h"
#include "../Report.h"
#include "../Module.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace WebUI {

    // Static member initialization
    HttpResponse HttpCommand::_last_response;

    // State check function for command registration
    bool http_state_check() {
        // Block during states where HTTP requests are inappropriate
        if (state_is(State::Homing)) {
            return true;  // Block
        }
        if (state_is(State::Jog)) {
            return true;  // Block
        }
        if (state_is(State::SafetyDoor)) {
            return true;  // Block
        }
        if (state_is(State::Sleep)) {
            return true;  // Block
        }
        return false;  // Allow
    }

    // Command handler
    Error http_command_handler(const char* value, AuthenticationLevel auth_level, Channel& out) {
        return HttpCommand::execute(value, auth_level, out);
    }

    // Module for registering the HTTP command during system initialization
    class HttpCommandModule : public Module {
    public:
        explicit HttpCommandModule(const char* name) : Module(name) {}

        void init() override {
            new UserCommand("HTTP", "Custom/HTTP", http_command_handler, http_state_check, WG);
            log_info("HTTP command registered");
        }
    };

    // Register module with init_priority to ensure it runs after core initialization
    // Second parameter 'true' means autocreate the module at startup
    ModuleFactory::InstanceBuilder<HttpCommandModule> http_command_module __attribute__((init_priority(110))) ("http_command", true);

    bool HttpCommand::state_check() {
        return http_state_check();
    }

    int HttpCommand::last_status_code() {
        return _last_response.status_code;
    }

    const std::string& HttpCommand::last_response_body() {
        return _last_response.body;
    }

    Error HttpCommand::execute(const char* value, AuthenticationLevel auth_level, Channel& out) {
        // Check WiFi connection
        if (WiFi.status() != WL_CONNECTED) {
            log_error_to(out, "HTTP: WiFi not connected");
            return Error::FsFailedOpenFile;  // Connection failure
        }

        // Parse command
        std::string url;
        std::string json_options;
        if (!parse_command(value, url, json_options)) {
            log_error_to(out, "HTTP: Invalid command format. Use: $HTTP=url or $HTTP=url{json}");
            return Error::InvalidStatement;
        }

        // Build request
        HttpRequest request;
        request.url = url;

        if (!json_options.empty() && !parse_json_options(json_options, request)) {
            log_error_to(out, "HTTP: Failed to parse JSON options");
            return Error::InvalidValue;
        }

        // Warn if in Cycle state
        if (state_is(State::Cycle)) {
            log_warn_to(out, "HTTP: Request during active motion may cause buffer underrun");
        }

        // Execute request
        HttpResponse response;
        Error        result = execute_request(request, response, out);

        // Store response in parameters
        _last_response = response;
        store_response_params(response);

        // Extract values from response if requested
        if (result == Error::Ok && !request.extract.empty()) {
            extract_response_values(request, response, out);
        }

        if (result == Error::Ok) {
            log_info_to(out, "HTTP: " << response.status_code);
        }

        return result;
    }

    bool HttpCommand::parse_command(const char* value, std::string& url, std::string& json_options) {
        // Format: url{json} or url
        // The value is everything after $HTTP=
        // URL ends at '{' (start of JSON) or end of string

        if (!value || *value == '\0') {
            return false;
        }

        // Find the start of JSON options (if any)
        const char* json_start = strchr(value, '{');

        if (json_start) {
            // URL is everything before the '{'
            url = std::string(value, json_start - value);

            // JSON is everything from '{' to matching '}'
            // Find matching closing brace (handle nested braces)
            int         brace_count = 1;
            const char* p           = json_start + 1;

            while (*p && brace_count > 0) {
                if (*p == '{') {
                    brace_count++;
                } else if (*p == '}') {
                    brace_count--;
                }
                p++;
            }

            if (brace_count != 0) {
                return false;  // Unbalanced braces
            }

            json_options = std::string(json_start, p - json_start);
        } else {
            // No JSON options, URL is the entire value
            url = value;
            json_options.clear();
        }

        // Validate URL is not empty
        if (url.empty()) {
            return false;
        }

        return true;
    }

    bool HttpCommand::extract_json_string(const std::string& json, const std::string& key, std::string& value) {
        // Simple JSON string extraction
        // Looks for "key":"value" pattern
        std::string search = "\"" + key + "\"";
        size_t      pos    = json.find(search);
        if (pos == std::string::npos) {
            return false;
        }

        pos += search.length();

        // Skip whitespace and colon
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':')) {
            pos++;
        }

        if (pos >= json.length() || json[pos] != '"') {
            return false;
        }

        pos++;  // Skip opening quote
        size_t start = pos;

        // Find closing quote (handle escape sequences)
        while (pos < json.length()) {
            if (json[pos] == '\\' && pos + 1 < json.length()) {
                pos += 2;  // Skip escaped character
            } else if (json[pos] == '"') {
                break;
            } else {
                pos++;
            }
        }

        if (pos >= json.length()) {
            return false;
        }

        value = json.substr(start, pos - start);

        // Unescape basic sequences
        size_t escape_pos = 0;
        while ((escape_pos = value.find('\\', escape_pos)) != std::string::npos) {
            if (escape_pos + 1 < value.length()) {
                char escaped = value[escape_pos + 1];
                char replacement;
                switch (escaped) {
                    case 'n':
                        replacement = '\n';
                        break;
                    case 'r':
                        replacement = '\r';
                        break;
                    case 't':
                        replacement = '\t';
                        break;
                    case '"':
                        replacement = '"';
                        break;
                    case '\\':
                        replacement = '\\';
                        break;
                    default:
                        replacement = escaped;
                        break;
                }
                value.replace(escape_pos, 2, 1, replacement);
            }
            escape_pos++;
        }

        return true;
    }

    bool HttpCommand::extract_json_object(const std::string& json, const std::string& key, std::string& value) {
        // Look for "key":{...} pattern
        std::string search = "\"" + key + "\"";
        size_t      pos    = json.find(search);
        if (pos == std::string::npos) {
            return false;
        }

        pos += search.length();

        // Skip whitespace and colon
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':')) {
            pos++;
        }

        if (pos >= json.length() || json[pos] != '{') {
            return false;
        }

        size_t start       = pos;
        int    brace_count = 1;
        pos++;

        while (pos < json.length() && brace_count > 0) {
            if (json[pos] == '{') {
                brace_count++;
            } else if (json[pos] == '}') {
                brace_count--;
            } else if (json[pos] == '"') {
                // Skip string content
                pos++;
                while (pos < json.length() && json[pos] != '"') {
                    if (json[pos] == '\\' && pos + 1 < json.length()) {
                        pos++;
                    }
                    pos++;
                }
            }
            pos++;
        }

        if (brace_count != 0) {
            return false;
        }

        value = json.substr(start, pos - start);
        return true;
    }

    bool HttpCommand::extract_json_number(const std::string& json, const std::string& key, int& value) {
        std::string search = "\"" + key + "\"";
        size_t      pos    = json.find(search);
        if (pos == std::string::npos) {
            return false;
        }

        pos += search.length();

        // Skip whitespace and colon
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':')) {
            pos++;
        }

        if (pos >= json.length() || (!isdigit(json[pos]) && json[pos] != '-')) {
            return false;
        }

        size_t start = pos;
        if (json[pos] == '-') {
            pos++;
        }
        while (pos < json.length() && isdigit(json[pos])) {
            pos++;
        }

        value = std::stoi(json.substr(start, pos - start));
        return true;
    }

    bool HttpCommand::extract_json_float(const std::string& json, const std::string& key, float& value) {
        std::string search = "\"" + key + "\"";
        size_t      pos    = json.find(search);
        if (pos == std::string::npos) {
            return false;
        }

        pos += search.length();

        // Skip whitespace and colon
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ':')) {
            pos++;
        }

        if (pos >= json.length() || (!isdigit(json[pos]) && json[pos] != '-' && json[pos] != '.')) {
            return false;
        }

        size_t start = pos;
        if (json[pos] == '-') {
            pos++;
        }
        while (pos < json.length() &&
               (isdigit(json[pos]) || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E' || json[pos] == '+' || json[pos] == '-')) {
            // Handle scientific notation properly - don't allow multiple signs except after e/E
            if ((json[pos] == '+' || json[pos] == '-') && pos > start && json[pos - 1] != 'e' && json[pos - 1] != 'E') {
                break;
            }
            pos++;
        }

        try {
            value = std::stof(json.substr(start, pos - start));
            return true;
        } catch (...) { return false; }
    }

    void HttpCommand::extract_response_values(const HttpRequest& request, const HttpResponse& response, Channel& out) {
        if (request.extract.empty()) {
            return;
        }

        log_debug("HTTP: Response body for extraction: " << response.body);

        for (const auto& mapping : request.extract) {
            const std::string& param_name = mapping.first;
            const std::string& json_key   = mapping.second;

            float value;
            if (extract_json_float(response.body, json_key, value)) {
                set_named_param(param_name.c_str(), value);
                log_debug("HTTP: Extracted " << param_name << " = " << value << " from key '" << json_key << "'");
            } else {
                log_warn_to(out, "HTTP: Failed to extract '" << json_key << "' for parameter " << param_name);
            }
        }
    }

    bool HttpCommand::parse_headers_object(const std::string& json_obj, std::map<std::string, std::string>& headers) {
        // Parse {"key":"value", "key2":"value2"} format
        size_t pos = 1;  // Skip opening brace

        while (pos < json_obj.length() - 1) {
            // Skip whitespace
            while (pos < json_obj.length() && (json_obj[pos] == ' ' || json_obj[pos] == ',' || json_obj[pos] == '\n')) {
                pos++;
            }

            if (pos >= json_obj.length() - 1 || json_obj[pos] == '}') {
                break;
            }

            if (json_obj[pos] != '"') {
                return false;
            }

            // Extract key
            pos++;
            size_t key_start = pos;
            while (pos < json_obj.length() && json_obj[pos] != '"') {
                pos++;
            }
            std::string key = json_obj.substr(key_start, pos - key_start);
            pos++;

            // Skip to value
            while (pos < json_obj.length() && (json_obj[pos] == ' ' || json_obj[pos] == ':')) {
                pos++;
            }

            if (pos >= json_obj.length() || json_obj[pos] != '"') {
                return false;
            }

            // Extract value
            pos++;
            size_t value_start = pos;
            while (pos < json_obj.length() && json_obj[pos] != '"') {
                if (json_obj[pos] == '\\' && pos + 1 < json_obj.length()) {
                    pos++;
                }
                pos++;
            }
            std::string value = json_obj.substr(value_start, pos - value_start);
            pos++;

            headers[key] = value;
        }

        return true;
    }

    bool HttpCommand::parse_json_options(const std::string& json, HttpRequest& request) {
        // Parse optional fields from JSON

        // Method (GET, POST, PUT, DELETE)
        std::string method;
        if (extract_json_string(json, "method", method)) {
            request.method = method;
        }

        // Timeout
        int timeout;
        if (extract_json_number(json, "timeout", timeout)) {
            request.timeout_ms = std::min(static_cast<uint32_t>(timeout), MAX_TIMEOUT_MS);
        }

        // Headers
        std::string headers_obj;
        if (extract_json_object(json, "headers", headers_obj)) {
            parse_headers_object(headers_obj, request.headers);
        }

        // Body - can be object or string
        std::string body_obj;
        if (extract_json_object(json, "body", body_obj)) {
            request.body = body_obj;
            // Default to POST if body is present and method not specified
            if (method.empty()) {
                request.method = "POST";
            }
        } else {
            std::string body_str;
            if (extract_json_string(json, "body", body_str)) {
                request.body = body_str;
                if (method.empty()) {
                    request.method = "POST";
                }
            }
        }

        // Extract - maps GCode parameter names to JSON keys to extract from response
        std::string extract_obj;
        if (extract_json_object(json, "extract", extract_obj)) {
            parse_headers_object(extract_obj, request.extract);  // Same format as headers
        }

        return true;
    }

    bool HttpCommand::parse_url(const std::string& url, std::string& protocol, std::string& host, uint16_t& port, std::string& path) {
        // Parse URL: protocol://host:port/path
        size_t protocol_end = url.find("://");
        if (protocol_end == std::string::npos) {
            return false;
        }

        protocol = url.substr(0, protocol_end);
        if (protocol != "http" && protocol != "https") {
            return false;
        }

        size_t host_start = protocol_end + 3;
        size_t path_start = url.find('/', host_start);
        if (path_start == std::string::npos) {
            path_start = url.length();
            path       = "/";
        } else {
            path = url.substr(path_start);
        }

        std::string host_port = url.substr(host_start, path_start - host_start);
        size_t      port_pos  = host_port.find(':');
        if (port_pos != std::string::npos) {
            host = host_port.substr(0, port_pos);
            port = std::stoi(host_port.substr(port_pos + 1));
        } else {
            host = host_port;
            port = (protocol == "https") ? 443 : 80;
        }

        return !host.empty();
    }

    Error HttpCommand::execute_request(const HttpRequest& request, HttpResponse& response, Channel& out) {
        std::string protocol;
        std::string host;
        uint16_t    port;
        std::string path;

        if (!parse_url(request.url, protocol, host, port, path)) {
            log_error_to(out, "HTTP: Invalid URL format");
            return Error::InvalidValue;
        }

        log_debug("HTTP: " << request.method << " " << protocol << "://" << host << ":" << port << path);

        // Create client based on protocol
        // HTTP: Use WiFiClient (plain TCP)
        // HTTPS: Use WiFiClientSecure (TLS/SSL)
        WiFiClient       http_client;
        WiFiClientSecure https_client;
        WiFiClient*      client_ptr;

        bool connected = false;
        if (protocol == "https") {
            https_client.setInsecure();  // Skip certificate validation
            https_client.setTimeout(request.timeout_ms / 1000);
            connected  = https_client.connect(host.c_str(), port);
            client_ptr = &https_client;
        } else {
            http_client.setTimeout(request.timeout_ms / 1000);
            connected  = http_client.connect(host.c_str(), port);
            client_ptr = &http_client;
        }

        if (!connected) {
            log_error_to(out, "HTTP: Connection failed to " << host << ":" << port);
            return Error::FsFailedOpenFile;  // Connection failure
        }

        WiFiClient& client = *client_ptr;

        // Build request
        std::string http_request;
        http_request += request.method + " " + path + " HTTP/1.1\r\n";
        http_request += "Host: " + host + "\r\n";
        http_request += "Connection: close\r\n";
        http_request += "User-Agent: FluidNC\r\n";

        // Add custom headers
        for (const auto& header : request.headers) {
            http_request += header.first + ": " + header.second + "\r\n";
        }

        // Add body
        if (!request.body.empty()) {
            // Check if body looks like JSON
            if (request.body[0] == '{') {
                http_request += "Content-Type: application/json\r\n";
            }
            http_request += "Content-Length: " + std::to_string(request.body.length()) + "\r\n";
            http_request += "\r\n";
            http_request += request.body;
        } else {
            http_request += "\r\n";
        }

        // Send request
        client.print(http_request.c_str());

        // Read response status line
        uint32_t start_time = millis();
        while (client.connected() && !client.available()) {
            if (millis() - start_time > request.timeout_ms) {
                client.stop();
                log_error_to(out, "HTTP: Response timeout");
                return Error::AnotherInterfaceBusy;  // Timeout
            }
            delay(10);
        }

        // Parse status line
        std::string status_line = client.readStringUntil('\n').c_str();
        log_debug("HTTP response: " << status_line);

        // Extract status code
        size_t space1 = status_line.find(' ');
        if (space1 != std::string::npos) {
            size_t space2 = status_line.find(' ', space1 + 1);
            if (space2 != std::string::npos) {
                response.status_code = std::stoi(status_line.substr(space1 + 1, space2 - space1 - 1));
            }
        }

        // Skip headers
        while (client.connected()) {
            std::string line = client.readStringUntil('\n').c_str();
            if (line.length() <= 1) {
                break;  // Empty line signals end of headers
            }
        }

        // Read body (limited size)
        // Note: Use OR not AND - server may close connection after sending, but data still in buffer
        response.body.clear();
        while ((client.connected() || client.available()) && response.body.length() < MAX_RESPONSE_SIZE) {
            if (client.available()) {
                char c = client.read();
                response.body += c;
            } else if (client.connected()) {
                delay(1);  // Wait for more data
            }
        }
        log_debug("HTTP: Read body length: " << response.body.length() << " bytes");

        client.stop();

        // Check for success (2xx status)
        if (response.status_code >= 200 && response.status_code < 300) {
            return Error::Ok;
        } else if (response.status_code >= 400) {
            log_warn_to(out, "HTTP: Server returned " << response.status_code);
            return Error::Ok;  // Still return Ok so GCode can check the status
        }

        return Error::Ok;
    }

    void HttpCommand::store_response_params(const HttpResponse& response) {
        // Store HTTP status code in named parameter
        set_named_param("_HTTP_STATUS", static_cast<float>(response.status_code));

        // Note: Response body is stored in _last_response and accessible via last_response_body()
        // We can't easily store strings in GCode parameters, so we store a hash or length
        set_named_param("_HTTP_RESPONSE_LEN", static_cast<float>(response.body.length()));
    }

}  // namespace WebUI
