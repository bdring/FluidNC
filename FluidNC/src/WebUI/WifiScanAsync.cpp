// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "WifiScanAsync.h"

#include "WifiImpl.h"  // wifiImpl(), encodeApList()
#include "../JSONEncoder.h"

#include <Arduino.h>  // millis()
#include <ESPAsyncWebServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cctype>
#include <memory>
#include <string>

using namespace asyncsrv;  // T_Cache_Control, T_no_cache

namespace WebUI {
    namespace {
        // Safety net so a pending request is never leaked if the scan never
        // finishes (e.g. the radio wedges).  The scan itself is watchdog-safe
        // because the request is paused while it runs.
        constexpr uint32_t WIFI_SCAN_TIMEOUT_MS = 25000;

        struct PendingScan {
            AsyncWebServerRequestPtr request;
            bool                     jsonWrapper;
            uint32_t                 startMs;
        };

        // beginAsyncWifiScan() runs on the async webserver task;
        // pollAsyncWifiScan() runs on the polling task.  The mutex guards the
        // handoff of s_pending between them and nothing else - in particular
        // the scan itself is driven only from the polling task, so the
        // (possibly blocking) startApListScan() call is made with the lock
        // released.  A FreeRTOS semaphore is used rather than std::mutex to
        // match the rest of FluidNC and stay portable across the toolchains
        // that build the WebUI.
        SemaphoreHandle_t            s_mutex = xSemaphoreCreateMutex();
        std::unique_ptr<PendingScan> s_pending;

        struct Lock {
            Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
            ~Lock() { xSemaphoreGive(s_mutex); }
        };

        bool isListApsCommand(const char* cmd, bool& jsonWrapper) {
            std::string s(cmd ? cmd : "");
            for (auto& c : s) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            jsonWrapper = s.find("JSON=YES") != std::string::npos;
            return s.rfind("[ESP410]", 0) == 0 || s.find("WIFI/LISTAPS") != std::string::npos;
        }

        // Builds the ESP410 response body from the completed scan results,
        // using the same encoder as the synchronous WiFiConfig::listAPs().
        std::string buildBody(int32_t count, bool jsonWrapper) {
            std::string body;
            JSONencoder j([&body](const char* text) { body += text; });
            encodeApList(j, count, jsonWrapper);
            return body;
        }
    }

    bool beginAsyncWifiScan(AsyncWebServerRequest* request, const char* cmd) {
        bool jsonWrapper;
        if (!isListApsCommand(cmd, jsonWrapper)) {
            return false;
        }

        Lock lock;
        if (s_pending) {
            request->send(503, "text/plain", "Wi-Fi scan already in progress\n");
            return true;
        }

        // Record the paused request; the scan is started (and, if it fails to
        // start, retried) from pollAsyncWifiScan() so no WiFi work runs on the
        // async webserver task here.
        s_pending.reset(new PendingScan { request->pause(), jsonWrapper, millis() });
        return true;
    }

    void pollAsyncWifiScan() {
        AsyncWebServerRequestPtr requestPtr;
        std::string              body;
        bool                     kickScan = false;

        {
            Lock lock;
            if (!s_pending) {
                return;
            }
            if (s_pending->request.expired()) {  // client disconnected while paused
                wifiImpl().finishApListScan();
                s_pending.reset();
                return;
            }

            WifiImpl::ApScanState state    = wifiImpl().apListScanState();
            bool                  timedOut = (millis() - s_pending->startMs) >= WIFI_SCAN_TIMEOUT_MS;
            if (state != WifiImpl::ApScanState::Done && !timedOut) {
                // Running: wait.  Failed (never started, or start failed):
                // (re)kick it - startApListScan() is idempotent - and wait.
                // Only the timeout below ends a scan that never completes.
                // Deferred until the lock is released: on rp2040/rp2350
                // startApListScan() blocks for the whole scan.
                kickScan = true;
            } else {
                int32_t count = (state == WifiImpl::ApScanState::Done) ? wifiImpl().apListCount() : 0;
                body          = buildBody(count, s_pending->jsonWrapper);
                wifiImpl().finishApListScan();
                requestPtr = std::move(s_pending->request);
                s_pending.reset();
            }
        }

        if (kickScan) {
            wifiImpl().startApListScan();
            return;
        }

        if (auto request = requestPtr.lock()) {
            AsyncWebServerResponse* response = request->beginResponse(200, "application/json", body.c_str());
            response->addHeader(T_Cache_Control, T_no_cache);
            request->send(response);
        }
    }
}
