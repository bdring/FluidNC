// Copyright (c) 2025 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// HTTP Command for FluidNC
// Allows GCode programs to make outgoing HTTP requests to external services.
//
// Usage: $HTTP=url or $HTTP=url{json_options}
//
// JSON options:
//   method  - HTTP method (GET, POST, PUT, DELETE). Default: GET (POST if body present)
//   timeout - Request timeout in ms (max 10000). Default: 5000
//   body    - Request body string
//   headers - Object of custom headers, e.g. {"Authorization":"Bearer xyz"}
//   extract       - Object mapping GCode params to JSON keys, e.g. {"_temp":"temperature"}
//   halt_on_error - If true (default), errors halt GCode. If false, errors set _HTTP_STATUS=0
//
// Token substitution:
//   Long tokens (like JWT auth tokens) can be stored in /localfs/http_settings.json and
//   referenced using ${token_name} syntax. This avoids the 255-char GCode line limit.
//
//   Settings file format (JSON):
//     {"tokens":{"ha_token":"Bearer eyJhbGciOiJIUzI1NiIs...","api_key":"sk-abc123"}}
//
//   Usage in GCode:
//     $HTTP=http://ha:8123/api{"headers":{"Authorization":"${ha_token}"}}
//
//   Tokens are loaded at startup. Use $HTTP/Tokens/Reload to reload without reboot.
//
// Examples:
//   $HTTP=http://example.com/api
//   $HTTP=http://example.com/api{"method":"POST","body":"{\"key\":\"value\"}"}
//   $HTTP=http://metrics.local/log{"halt_on_error":false}
//   $HTTP=http://ha:8123/api{"headers":{"Authorization":"${ha_token}"}}
//
// GCode parameters set after request:
//   _HTTP_STATUS       - HTTP status code (0 if connection failed)
//   _HTTP_RESPONSE_LEN - Response body length
//   Plus any parameters defined in "extract" option
//
// Note: All GCode parameters are floats. Only numeric JSON values can be extracted.
// String values in JSON responses cannot be stored in parameters.
//
// Example with extraction:
//   ; Server returns: {"temperature": 25.5, "pressure": 101.3}
//   $HTTP=http://sensor/api{"extract":{"_temp":"temperature","_psi":"pressure"}}
//   ; Now use in GCode:
//   #<_safe> = [#<_temp> LT 50]  ; Check if temperature < 50
//   o100 if [#<_safe> EQ 0]
//     M0 (Temperature too high!)
//   o100 endif
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
#include "../FluidPath.h"
#include "Driver/localfs.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace WebUI {

    // Static member initialization
    HttpResponse                       HttpCommand::_last_response;
    std::map<std::string, std::string> HttpCommand::_tokens;

    // ============================================================================
    // Token loading and substitution
    // ============================================================================

    void HttpCommand::load_tokens() {
        _tokens.clear();

        // Build full path using FluidNC's localfs prefix
        std::string fullPath = "/";
        fullPath += localfsName;
        fullPath += SETTINGS_FILE_PATH;

        FILE* file = fopen(fullPath.c_str(), "r");
        if (!file) {
            log_debug("HTTP: No settings file at " << fullPath);
            return;
        }

        // Read entire file content
        std::string content;
        int         c;
        while ((c = fgetc(file)) != EOF) {
            content += static_cast<char>(c);
        }
        fclose(file);

        // Parse JSON using streaming parser
        JsonStreamingParser parser;
        TokenFileListener   listener(_tokens);
        parser.setListener(&listener);

        for (char c : content) {
            parser.parse(c);
        }

        log_info("HTTP: Loaded " << _tokens.size() << " tokens from " << SETTINGS_FILE_PATH);
        for (const auto& token : _tokens) {
            log_debug("HTTP: Token '" << token.first << "' (" << token.second.length() << " chars)");
        }
    }

    std::string HttpCommand::substitute_tokens(const std::string& input) {
        std::string result = input;
        size_t      pos    = 0;

        while ((pos = result.find("${", pos)) != std::string::npos) {
            size_t end = result.find("}", pos + 2);
            if (end == std::string::npos) {
                // No closing brace, stop processing
                break;
            }

            std::string token_name = result.substr(pos + 2, end - pos - 2);
            auto        it         = _tokens.find(token_name);

            if (it != _tokens.end()) {
                result.replace(pos, end - pos + 1, it->second);
                // Move past the substituted value
                pos += it->second.length();
            } else {
                log_warn("HTTP: Unknown token '${" << token_name << "}'");
                // Move past this token reference to avoid infinite loop
                pos = end + 1;
            }
        }

        return result;
    }

    // ============================================================================
    // HttpOptionsListener implementation
    // Parses JSON like: {"method":"POST","timeout":5000,"headers":{...},"extract":{...}}
    // ============================================================================

    void HttpOptionsListener::startDocument() {
        _depth     = 0;
        _inHeaders = false;
        _inExtract = false;
        _currentKey.clear();
        _nestedKey.clear();
    }

    void HttpOptionsListener::key(String key) {
        if (_depth == 1) {
            _currentKey = key;
        } else if (_depth == 2 && (_inHeaders || _inExtract)) {
            _nestedKey = key;
        }
    }

    void HttpOptionsListener::value(String value) {
        if (_depth == 1) {
            // Top-level values
            if (_currentKey == "method") {
                _request.method = value.c_str();
            } else if (_currentKey == "timeout") {
                int timeout         = value.toInt();
                _request.timeout_ms = std::min(static_cast<uint32_t>(timeout), HttpCommand::MAX_TIMEOUT_MS);
            } else if (_currentKey == "body") {
                _request.body = value.c_str();
                // Default to POST if body is present and method not set
                if (_request.method == "GET") {
                    _request.method = "POST";
                }
            } else if (_currentKey == "halt_on_error") {
                // "halt_on_error":true (default) = halt GCode on error, "halt_on_error":false = continue
                _request.fail_on_error = (value == "true" || value == "1");
            }
        } else if (_depth == 2) {
            // Nested object values (headers or extract)
            if (_inHeaders && !_nestedKey.isEmpty()) {
                _request.headers[_nestedKey.c_str()] = value.c_str();
            } else if (_inExtract && !_nestedKey.isEmpty()) {
                _request.extract[_nestedKey.c_str()] = value.c_str();
            }
        }
    }

    void HttpOptionsListener::startObject() {
        _depth++;
        if (_depth == 2) {
            if (_currentKey == "headers") {
                _inHeaders = true;
            } else if (_currentKey == "extract") {
                _inExtract = true;
            } else if (_currentKey == "body") {
                // Body can be an object - we'll need to capture it differently
                // For now, treat nested body objects as strings
            }
        }
    }

    void HttpOptionsListener::endObject() {
        if (_depth == 2) {
            _inHeaders = false;
            _inExtract = false;
        }
        _depth--;
    }

    void HttpOptionsListener::startArray() {
        _depth++;
    }

    void HttpOptionsListener::endArray() {
        _depth--;
    }

    void HttpOptionsListener::endDocument() {
        // Nothing to do
    }

    // ============================================================================
    // ValueExtractorListener implementation
    // Extracts specific float values from JSON response body
    // ============================================================================

    void ValueExtractorListener::key(String key) {
        if (_depth == 1) {
            _currentKey = key;
        }
    }

    void ValueExtractorListener::value(String value) {
        if (_depth == 1 && !_currentKey.isEmpty()) {
            // Check if this key should be extracted
            std::string keyStr = _currentKey.c_str();
            for (const auto& mapping : _extractMap) {
                if (mapping.second == keyStr) {
                    // Try to convert value to float
                    float floatVal          = value.toFloat();
                    _results[mapping.first] = floatVal;
                }
            }
        }
    }

    void ValueExtractorListener::startObject() {
        _depth++;
    }

    void ValueExtractorListener::endObject() {
        _depth--;
    }

    // ============================================================================
    // TokenFileListener implementation
    // Parses JSON like: {"tokens":{"token_name":"token_value",...}}
    // ============================================================================

    void TokenFileListener::startDocument() {
        _depth    = 0;
        _inTokens = false;
        _currentKey.clear();
    }

    void TokenFileListener::key(String key) {
        _currentKey = key;
        if (_depth == 1 && key == "tokens") {
            // Next startObject will be the tokens object
        }
    }

    void TokenFileListener::value(String value) {
        if (_inTokens && _depth == 2 && !_currentKey.isEmpty()) {
            _tokens[_currentKey.c_str()] = value.c_str();
        }
    }

    void TokenFileListener::startObject() {
        _depth++;
        if (_depth == 2 && _currentKey == "tokens") {
            _inTokens = true;
        }
    }

    void TokenFileListener::endObject() {
        if (_depth == 2 && _inTokens) {
            _inTokens = false;
        }
        _depth--;
    }

    // ============================================================================
    // State check and command handler
    // ============================================================================

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

    Error http_command_handler(const char* value, AuthenticationLevel auth_level, Channel& out) {
        return HttpCommand::execute(value, auth_level, out);
    }

    Error http_tokens_reload_handler(const char* value, AuthenticationLevel auth_level, Channel& out) {
        HttpCommand::load_tokens();
        return Error::Ok;
    }

    // Module for registering the HTTP command during system initialization
    class HttpCommandModule : public Module {
    public:
        explicit HttpCommandModule(const char* name) : Module(name) {}

        void init() override {
            new UserCommand("HTTP", "Custom/HTTP", http_command_handler, http_state_check, WG);
            new UserCommand("HTTP/Tokens/Reload", "Custom/HTTP/Tokens/Reload", http_tokens_reload_handler, anyState, WA);
            HttpCommand::load_tokens();
            log_info("HTTP command registered");
        }
    };

    // Register module with init_priority to ensure it runs after core initialization
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

    // ============================================================================
    // Main execute function
    // ============================================================================

    Error HttpCommand::execute(const char* value, AuthenticationLevel auth_level, Channel& out) {
        // Parse command first so we can check fail_on_error
        std::string url;
        std::string json_options;
        if (!parse_command(value, url, json_options)) {
            log_error_to(out, "HTTP: Invalid command format. Use: $HTTP=url or $HTTP=url{json}");
            return Error::InvalidStatement;  // Syntax errors always fail
        }

        // Build request
        HttpRequest request;
        request.url = url;

        if (!json_options.empty() && !parse_json_options(json_options, request)) {
            log_error_to(out, "HTTP: Failed to parse JSON options");
            return Error::InvalidValue;  // Syntax errors always fail
        }

        // Substitute tokens (${token_name} patterns) in URL, headers, and body
        request.url  = substitute_tokens(request.url);
        request.body = substitute_tokens(request.body);
        for (auto& header : request.headers) {
            header.second = substitute_tokens(header.second);
        }

        // Check WiFi connection (runtime error - respects fail_on_error)
        if (WiFi.status() != WL_CONNECTED) {
            log_error_to(out, "HTTP: WiFi not connected");
            if (request.fail_on_error) {
                return Error::MessageFailed;
            }
            // Store zero status to indicate failure
            _last_response = HttpResponse();
            store_response_params(_last_response);
            return Error::Ok;
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

        // If fail_on_error is false, convert errors to Ok
        if (result != Error::Ok && !request.fail_on_error) {
            return Error::Ok;
        }

        return result;
    }

    // ============================================================================
    // Command parsing
    // ============================================================================

    bool HttpCommand::parse_command(const char* value, std::string& url, std::string& json_options) {
        // Format: url{json} or url
        // Note: ${...} is a token substitution pattern, NOT JSON start
        if (!value || *value == '\0') {
            return false;
        }

        // Find the start of JSON options (if any)
        // Skip ${...} patterns - JSON starts with { that is NOT preceded by $
        const char* json_start = nullptr;
        const char* p          = value;
        while (*p) {
            if (*p == '{') {
                // Check if this is a token pattern (preceded by $)
                if (p > value && *(p - 1) == '$') {
                    // This is ${...}, skip to the closing }
                    p++;
                    while (*p && *p != '}') {
                        p++;
                    }
                    if (*p == '}') {
                        p++;
                    }
                    continue;
                }
                // This is the start of JSON options
                json_start = p;
                break;
            }
            p++;
        }

        if (json_start) {
            // URL is everything before the '{'
            url = std::string(value, json_start - value);

            // JSON is everything from '{' to matching '}'
            int brace_count = 1;
            p               = json_start + 1;

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
            url = value;
            json_options.clear();
        }

        return !url.empty();
    }

    // ============================================================================
    // JSON parsing using streaming parser
    // ============================================================================

    bool HttpCommand::parse_json_options(const std::string& json, HttpRequest& request) {
        JsonStreamingParser parser;
        HttpOptionsListener listener(request);
        parser.setListener(&listener);

        for (char c : json) {
            parser.parse(c);
        }

        return true;
    }

    void HttpCommand::extract_response_values(const HttpRequest& request, const HttpResponse& response, Channel& out) {
        if (request.extract.empty()) {
            return;
        }

        log_debug("HTTP: Response body for extraction: " << response.body);

        // Use streaming parser to extract values
        std::map<std::string, float> results;
        JsonStreamingParser          parser;
        ValueExtractorListener       listener(request.extract, results);
        parser.setListener(&listener);

        for (char c : response.body) {
            parser.parse(c);
        }

        // Store extracted values in GCode parameters
        for (const auto& result : results) {
            set_named_param(result.first.c_str(), result.second);
            log_debug("HTTP: Extracted " << result.first << " = " << result.second);
        }

        // Report any keys that weren't found
        for (const auto& mapping : request.extract) {
            if (results.find(mapping.first) == results.end()) {
                log_warn_to(out, "HTTP: Failed to extract '" << mapping.second << "' for parameter " << mapping.first);
            }
        }
    }

    // ============================================================================
    // URL parsing
    // ============================================================================

    bool HttpCommand::parse_url(const std::string& url, std::string& protocol, std::string& host, uint16_t& port, std::string& path) {
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

    // ============================================================================
    // HTTP request execution
    // ============================================================================

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
        WiFiClient       http_client;
        WiFiClientSecure https_client;
        WiFiClient*      client_ptr;

        bool connected = false;
        if (protocol == "https") {
            https_client.setInsecure();
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
            return Error::MessageFailed;
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

        // Read response
        uint32_t start_time = millis();
        while (client.connected() && !client.available()) {
            if (millis() - start_time > request.timeout_ms) {
                client.stop();
                log_error_to(out, "HTTP: Response timeout");
                return Error::AnotherInterfaceBusy;
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
                break;
            }
        }

        // Read body
        response.body.clear();
        while ((client.connected() || client.available()) && response.body.length() < MAX_RESPONSE_SIZE) {
            if (client.available()) {
                char c = client.read();
                response.body += c;
            } else if (client.connected()) {
                delay(1);
            }
        }
        log_debug("HTTP: Read body length: " << response.body.length() << " bytes");

        client.stop();

        if (response.status_code >= 200 && response.status_code < 300) {
            return Error::Ok;
        } else if (response.status_code >= 400) {
            log_warn_to(out, "HTTP: Server returned " << response.status_code);
            return Error::Ok;
        }

        return Error::Ok;
    }

    void HttpCommand::store_response_params(const HttpResponse& response) {
        set_named_param("_HTTP_STATUS", static_cast<float>(response.status_code));
        set_named_param("_HTTP_RESPONSE_LEN", static_cast<float>(response.body.length()));
    }

}  // namespace WebUI
