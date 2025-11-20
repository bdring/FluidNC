// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "FileStream.h"

#include "Settings.h"
#include "Module.h"

#include "Authentication.h"  // AuthenticationLevel

#include <freertos/task.h>

class AsyncWebSocket;
class AsyncWebServer;
class AsyncWebSocketMessageHandler;
class AsyncHeaderFreeMiddleware;
class AsyncWebServerRequest;
class AsyncClient;

namespace WebUI {
    static const int DEFAULT_HTTP_STATE                 = 1;
    static const int DEFAULT_HTTP_BLOCKED_DURING_MOTION = 1;
    static const int DEFAULT_HTTP_PORT                  = 80;

    static const int MIN_HTTP_PORT = 1;
    static const int MAX_HTTP_PORT = 65001;

    extern EnumSetting* http_enable;
    extern IntSetting*  http_port;

#ifdef ENABLE_AUTHENTICATION
    struct AuthenticationIP {
        IPAddress           ip;
        AuthenticationLevel level;
        char                userID[17];
        char                sessionID[17];
        uint32_t            last_time;
        AuthenticationIP*   _next;
    };
#endif

    std::string getSession(AsyncClient* client);

    //Upload status
    enum class UploadStatus : uint8_t { NONE = 0, FAILED = 1, CANCELLED = 2, SUCCESSFUL = 3, ONGOING = 4 };

    class WebUI_Server : public Module {
    public:
        WebUI_Server(const char* name) : Module(name) {}

        void init() override;
        void deinit() override;
        void poll() override;

        static uint16_t port() { return _port; }

        ~WebUI_Server();

    private:
        static bool                       _setupdone;
        static AsyncWebServer*            _webserver;
        static AsyncHeaderFreeMiddleware* _headerFilter;
        static AsyncWebServer*            _websocketserver;
        static AsyncWebServer*            _websocketserverv3;
        static AsyncWebSocket*            _socket_server;
        static std::string                current_session;

        static uint16_t     _port;
        static UploadStatus _upload_status;
        static FileStream*  _uploadFile;
        static bool         _schedule_reboot;
        static uint32_t     _schedule_reboot_time;

        static AuthenticationLevel is_authenticated();
#ifdef ENABLE_AUTHENTICATION
        static AuthenticationIP*   _head;
        static uint8_t             _nb_ip;
        static bool                AddAuthIP(AuthenticationIP* item);
        static const char*         create_session_ID();
        static bool                ClearAuthIP(IPAddress ip, const char* sessionID);
        static AuthenticationIP*   GetAuth(IPAddress ip, const char* sessionID);
        static AuthenticationLevel ResetAuthIP(IPAddress ip, const char* sessionID);
#endif
        static std::string getSessionCookie(AsyncWebServerRequest* request);
        static void        handle_SSDP();
        static void        handle_root(AsyncWebServerRequest* request);
        static void        handle_login(AsyncWebServerRequest* request);
        static void        handle_not_found(AsyncWebServerRequest* request);
        static void        _handle_web_command(AsyncWebServerRequest* request, bool);
        static void        handle_web_command(AsyncWebServerRequest* request) {
                   _handle_web_command(request, false);
        }
        static void handle_web_command_silent(AsyncWebServerRequest* request) {
            _handle_web_command(request, true);
        }
        static void handleReloadBlocked(AsyncWebServerRequest* request);
        static void handleFeedholdReload(AsyncWebServerRequest* request);
        static void handleCyclestartReload(AsyncWebServerRequest* request);
        static void handleRestartReload(AsyncWebServerRequest* request);
        static void handleDidRestart(AsyncWebServerRequest* request);
        static void LocalFSFileupload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
        static void handleFileList(AsyncWebServerRequest* request);
        static void handleUpdate(AsyncWebServerRequest* request);
        static void WebUpdateUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);

        static bool myStreamFile(AsyncWebServerRequest* request, const char* path, bool download = false, bool setSession = false);

        static void pushError(AsyncWebServerRequest* request, uint16_t code, const char* st, int32_t web_error = 500, uint16_t timeout = 1000);
        static void cancelUpload(AsyncWebServerRequest* request);
        static void handleFileOps(AsyncWebServerRequest* request, const char* mountpoint);
        static void handle_direct_SDFileList(AsyncWebServerRequest* request);
        static void fileUpload(
            AsyncWebServerRequest* request, const char* fs, String filename, size_t index, uint8_t* data, size_t len, bool final);
        static void SDFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final);
        static void uploadStart(AsyncWebServerRequest* request, const char* filename, size_t filesize, const char* fs);
        static void uploadWrite(AsyncWebServerRequest* request, uint8_t* buffer, size_t length);
        static void uploadEnd(AsyncWebServerRequest* request, size_t filesize);
        static void uploadStop();
        static void uploadCheck(AsyncWebServerRequest* request);

        static bool isAllowedInMotion(String cmd);
        static void synchronousCommand(
            AsyncWebServerRequest* request, const char* cmd, bool silent, AuthenticationLevel auth_level, bool allowedInMotion = false);
        static void websocketCommand(AsyncWebServerRequest* request, const char* cmd, uint32_t pageid, AuthenticationLevel auth_level);

        static void sendJSON(AsyncWebServerRequest* request, uint16_t code, const char* s);
        static void sendJSON(AsyncWebServerRequest* request, uint16_t code, const std::string& s) {
            sendJSON(request, code, s.c_str());
        }
        static void sendAuth(AsyncWebServerRequest* request, const char* status, const char* level, const char* user);
        static void sendAuthFailed(AsyncWebServerRequest* request);
        static void sendStatus(AsyncWebServerRequest* request, uint16_t code, const char* str);

        static void sendWithOurAddress(AsyncWebServerRequest* request, const char* s, uint16_t code);
        static void sendCaptivePortal(AsyncWebServerRequest* request);
        static void send404Page(AsyncWebServerRequest* request);

        static uint32_t getPageid(AsyncWebServerRequest* request);
    };
}
