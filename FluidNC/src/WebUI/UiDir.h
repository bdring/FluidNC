// Copyright (c) 2026 Mitch Bradley. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

class AsyncWebServer;
class AsyncWebSocket;
class AsyncWebServerRequest;

namespace WebUI {
    // Serves several WebUIs at once, each from a subdirectory of HTTP/UIDir
    // (default /sd/ui):
    //   /ui/                         a list of the WebUIs
    //   /ui/<name>/                  that WebUI's index.html, and its WebSocket
    //   /ui/<name>/command[_silent]  commands in that WebUI's session
    //   /ui/<name>/<path>            GET or PUT a file in that WebUI's directory
    // HTTP/DefaultUI names the one that / redirects to.
    class UiDir {
    public:
        static void init();  // Create the settings
        static void addHandlers(AsyncWebServer* server, AsyncWebSocket* ws);
        static void announce();  // Tell HashFS the directory, before it hashes

        // Redirect / to HTTP/DefaultUI if that WebUI is present
        static bool redirectToDefault(AsyncWebServerRequest* request);

    private:
        static bool indexExists(const std::string& name);
        static void handle(AsyncWebServerRequest* request);
        static void handleList(AsyncWebServerRequest* request, const std::string& dir);
        static void handleBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total);
        static void handlePut(AsyncWebServerRequest* request, const std::string& path);
    };
}
