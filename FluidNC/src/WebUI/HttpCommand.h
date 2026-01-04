// Copyright (c) 2025 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Settings.h"
#include "../Channel.h"

#include <string>
#include <map>

namespace WebUI {

    // HTTP request configuration parsed from JSON options
    struct HttpRequest {
        std::string                        url;
        std::string                        method;
        std::map<std::string, std::string> headers;
        std::string                        body;
        uint32_t                           timeout_ms;
        std::map<std::string, std::string> extract;  // Maps GCode param name -> JSON key to extract

        HttpRequest() : method("GET"), timeout_ms(5000) {}
    };

    // HTTP response data
    struct HttpResponse {
        int         status_code;
        std::string body;

        HttpResponse() : status_code(0) {}
    };

    class HttpCommand {
    public:
        // Maximum response body size to store
        static const size_t MAX_RESPONSE_SIZE = 256;

        // Maximum request timeout in milliseconds
        static const uint32_t MAX_TIMEOUT_MS = 10000;

        // Default request timeout in milliseconds
        static const uint32_t DEFAULT_TIMEOUT_MS = 5000;

        // Execute HTTP command
        // Format: $HTTP[url]{json_options}
        static Error execute(const char* value, AuthenticationLevel auth_level, Channel& out);

        // Check if current state allows HTTP requests
        static bool state_check();

        // Get last HTTP status code (for parameter access)
        static int last_status_code();

        // Get last HTTP response body (for parameter access)
        static const std::string& last_response_body();

    private:
        // Parse the command string into URL and JSON options
        static bool parse_command(const char* value, std::string& url, std::string& json_options);

        // Parse JSON options into HttpRequest struct
        static bool parse_json_options(const std::string& json, HttpRequest& request);

        // Parse URL into host, port, and path
        static bool parse_url(const std::string& url, std::string& protocol, std::string& host, uint16_t& port, std::string& path);

        // Execute the HTTP request
        static Error execute_request(const HttpRequest& request, HttpResponse& response, Channel& out);

        // Store response in GCode parameters
        static void store_response_params(const HttpResponse& response);

        // Simple JSON string extraction helper
        static bool extract_json_string(const std::string& json, const std::string& key, std::string& value);

        // Simple JSON object extraction helper
        static bool extract_json_object(const std::string& json, const std::string& key, std::string& value);

        // Simple JSON number extraction helper (integer)
        static bool extract_json_number(const std::string& json, const std::string& key, int& value);

        // Simple JSON number extraction helper (float)
        static bool extract_json_float(const std::string& json, const std::string& key, float& value);

        // Extract values from response and store in GCode parameters
        static void extract_response_values(const HttpRequest& request, const HttpResponse& response, Channel& out);

        // Parse headers object into map
        static bool parse_headers_object(const std::string& json_obj, std::map<std::string, std::string>& headers);

        // Last response data (for parameter access)
        static HttpResponse _last_response;
    };

    // Command handler for UserCommand registration
    Error http_command_handler(const char* value, AuthenticationLevel auth_level, Channel& out);

    // State check for UserCommand registration
    bool http_state_check();

}  // namespace WebUI
