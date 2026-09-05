// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

class AsyncWebServerRequest;

namespace WebUI {
    // A WiFi AP scan (ESP410 / [WiFi/ListAPs]) can take several seconds.
    // Running it inline stalls the web command task and can trip the HTTP
    // connection watchdog.  These helpers move it off the request path:
    // beginAsyncWifiScan() pauses the request and kicks off an asynchronous
    // scan through wifiImpl(); pollAsyncWifiScan() (called from
    // WebUI_Server::poll()) sends the response once results are ready.
    //
    // All WiFi specifics live behind wifiImpl(), so this module is portable
    // across the MCU builds that compile the WebUI (esp32, esp32s3, rp2040,
    // rp2350).  It is excluded from the host/simulator build, which has no
    // wifiImpl().

    // Returns true if `cmd` is an AP-scan web command: the request has been
    // paused (or answered with an error) and the caller must not touch it.
    // Returns false for any other command.
    bool beginAsyncWifiScan(AsyncWebServerRequest* request, const char* cmd);

    // Completes a pending paused scan request.  Call once per poll cycle.
    void pollAsyncWifiScan();
}
