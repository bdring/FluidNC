// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/FileStream.h"

#include "src/Settings.h"
#include "src/Module.h"

#include "Authentication.h"  // AuthenticationLevel

class AsyncWebSocket;
class AsyncWebServer;
class AsyncWebSocketMessageHandler;
class AsyncHeaderFreeMiddleware;
class AsyncWebServerRequest;

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

    //Upload status
    enum class UploadStatus : uint8_t { NONE = 0, FAILED = 1, CANCELLED = 2, SUCCESSFUL = 3, ONGOING = 4 };

    class WebUI_Server : public Module {
    public:
        WebUI_Server(const char* name) : Module(name) {}

        void init() override;
        void deinit() override;
        void poll() override;

        static long     get_client_ID();
        static uint16_t port() { return _port; }

        ~WebUI_Server();

    private:
        static bool              _setupdone;
        static AsyncWebServer*    _webserver;
        static AsyncHeaderFreeMiddleware* _headerFilter;
        static AsyncWebSocket* _socket_server;
        static AsyncWebSocket* _socket_serverv3;

        static uint16_t          _port;
        static UploadStatus      _upload_status;
        static FileStream*       _uploadFile;
        static std::map<std::string, FileStream*> _fileStreams;

        static const char* getContentType(const char* filename);

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
        static void handle_SSDP();
        static void handle_root(AsyncWebServerRequest *request);
        static void handle_login(AsyncWebServerRequest *request);
        static void handle_not_found(AsyncWebServerRequest *request);
        static void _handle_web_command(AsyncWebServerRequest *request, bool);
        static void handle_web_command(AsyncWebServerRequest *request) {
            _handle_web_command(request, false);
        }
        static void handle_web_command_silent(AsyncWebServerRequest *request) {
            _handle_web_command(request, true);
        }
        static void handleReloadBlocked(AsyncWebServerRequest *request);
        static void handleFeedholdReload(AsyncWebServerRequest *request);
        static void handleCyclestartReload(AsyncWebServerRequest *request);
        static void handleRestartReload(AsyncWebServerRequest *request);
        static void handleDidRestart(AsyncWebServerRequest *request);
        static void LocalFSFileupload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
        static void handleFileList(AsyncWebServerRequest *request);
        static void handleUpdate(AsyncWebServerRequest *request);
        static void WebUpdateUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

        static bool myStreamFile(AsyncWebServerRequest *request, const char* path, bool download = false);

        static void pushError(AsyncWebServerRequest *request, int code, const char* st, bool web_error = 500, uint16_t timeout = 1000);
        static FileStream* getFileStream(const char *path);
        static void cancelUpload(AsyncWebServerRequest *request);
        static void handleFileOps(AsyncWebServerRequest *request, const char* mountpoint);
        static void handle_direct_SDFileList(AsyncWebServerRequest *request);
        static void fileUpload(AsyncWebServerRequest *request, const char* fs, String filename, size_t index, uint8_t *data, size_t len, bool final);
        static void SDFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
        // static void uploadStart(const char* filename, size_t filesize, const char* fs);
        // static void uploadWrite(uint8_t* buffer, size_t length);
        // static void uploadEnd(size_t filesize);
        // static void uploadStop();
        // static void uploadCheck();

        static void synchronousCommand(AsyncWebServerRequest *request, const char* cmd, bool silent, AuthenticationLevel auth_level);
        static void websocketCommand(AsyncWebServerRequest *request, const char* cmd, int pageid, AuthenticationLevel auth_level);

        static void sendFSError(Error err);
        static void sendJSON(AsyncWebServerRequest *request, int code, const char* s);
        static void sendJSON(AsyncWebServerRequest *request, int code, const std::string& s) {
            sendJSON(request, code, s.c_str());
        }
        static void sendAuth(AsyncWebServerRequest *request, const char* status, const char* level, const char* user);
        static void sendAuthFailed(AsyncWebServerRequest *request);
        static void sendStatus(AsyncWebServerRequest *request, int code, const char* str);

        static void sendWithOurAddress(AsyncWebServerRequest *request, const char* s, int code);
        static void sendCaptivePortal(AsyncWebServerRequest *request);
        static void send404Page(AsyncWebServerRequest *request);

        static int getPageid(AsyncWebServerRequest *request);
    };
}
