// Copyright (c) 2025 - FluidNC Contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Settings.h"
#include "../Channel.h"

#include <JsonListener.h>
#include <JsonStreamingParser.h>
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
        std::map<std::string, std::string> extract;        // Maps GCode param name -> JSON key to extract
        bool                               fail_on_error;  // If true, errors halt GCode execution

        HttpRequest() : method("GET"), timeout_ms(5000), fail_on_error(true) {}
    };

    // JSON listener for parsing HTTP command options
    // Handles: {"method":"POST","timeout":5000,"headers":{...},"body":"...","extract":{...}}
    class HttpOptionsListener : public JsonListener {
    public:
        explicit HttpOptionsListener(HttpRequest& request) : _request(request) {}

        void whitespace(char c) override {}
        void startDocument() override;
        void key(String key) override;
        void value(String value) override;
        void startObject() override;
        void endObject() override;
        void startArray() override;
        void endArray() override;
        void endDocument() override;

    private:
        HttpRequest& _request;
        String       _currentKey;
        String       _nestedKey;
        int          _depth     = 0;
        bool         _inHeaders = false;
        bool         _inExtract = false;
    };

    // JSON listener for extracting float values from response
    // Used to extract specific keys from JSON response body
    // Calls set_named_param directly and removes found keys from extractMap
    class ValueExtractorListener : public JsonListener {
    public:
        explicit ValueExtractorListener(std::map<std::string, std::string>& extractMap, Channel& out) :
            _extractMap(extractMap), _out(out) {}

        void whitespace(char c) override {}
        void startDocument() override {}
        void key(String key) override;
        void value(String value) override;
        void startObject() override;
        void endObject() override;
        void startArray() override {}
        void endArray() override {}
        void endDocument() override {}

    private:
        std::map<std::string, std::string>& _extractMap;
        Channel&                            _out;
        String                              _currentKey;
        int                                 _depth = 0;
    };

    // JSON listener for parsing http_settings.json file
    // Format: {"tokens":{"token_name":"token_value",...},"commands":{"command_name":"command_value",...}}
    class TokenFileListener : public JsonListener {
    public:
        explicit TokenFileListener(std::map<std::string, std::string>& tokens, std::map<std::string, std::string>& commands) :
            _tokens(tokens), _commands(commands) {}

        void whitespace(char c) override {}
        void startDocument() override;
        void key(String key) override;
        void value(String value) override;
        void startObject() override;
        void endObject() override;
        void startArray() override {}
        void endArray() override {}
        void endDocument() override {}

    private:
        std::map<std::string, std::string>& _tokens;
        std::map<std::string, std::string>& _commands;
        String                              _currentKey;
        int                                 _depth    = 0;
        bool                                _inTokens = false;
        bool                                _inCommands = false;
    };

    class HttpCommand {
    public:
        // Maximum request timeout in milliseconds
        inline static constexpr uint32_t MAX_TIMEOUT_MS = 10000;

        // Default request timeout in milliseconds
        inline static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;

        // Settings file path on LocalFS (stores tokens, etc.)
        static constexpr const char* SETTINGS_FILE_PATH = "/http_settings.json";

        // Execute HTTP command
        // Format: $HTTP=url{json_options}
        static Error execute(const char* value, AuthenticationLevel auth_level, Channel& out);

        // Load tokens from file (called at init and by reload command)
        static void load_tokens();

        // Substitute ${token_name} patterns in a string
        static std::string substitute_tokens(const std::string& input);

    private:
        // Parse the command string into URL and JSON options
        static bool parse_command(const char* value, std::string& url, std::string& json_options);

        // Parse JSON options into HttpRequest struct using streaming parser
        static bool parse_json_options(const std::string& json, HttpRequest& request);

        // Parse URL into host, port, and path
        static bool parse_url(const std::string& url, std::string& protocol, std::string& host, uint16_t& port, std::string& path);

        // Execute the HTTP request
        // Returns: Error code
        // Output parameters: status_code (HTTP response code), bytes_received (response body size)
        static Error execute_request(HttpRequest& request, int& status_code, uint32_t& bytes_received, Channel& out);

        // Store response in GCode parameters
        static void store_response_params(int status_code, uint32_t bytes_received);

        // Token storage (loaded from TOKEN_FILE_PATH)
        static std::map<std::string, std::string> _tokens;

        // Command storage (loaded from TOKEN_FILE_PATH)
        static std::map<std::string, std::string> _commands;
    };

    // Command handler for UserCommand registration
    Error http_command_handler(const char* value, AuthenticationLevel auth_level, Channel& out);

    // State check for UserCommand registration
    bool http_state_check();

}  // namespace WebUI
