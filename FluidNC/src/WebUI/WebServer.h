// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*
#include "../FileStream.h"

#ifdef ENABLE_WIFI

#    include "../Settings.h"
#    include "Authentication.h"  // AuthenticationLevel
#    include "Commands.h"
#    include <FS.h>

class WebSocketsServer;
class WebServer;

namespace WebUI {
    static const int DEFAULT_HTTP_STATE = 1;
    static const int DEFAULT_HTTP_PORT  = 80;

    static const int MIN_HTTP_PORT = 1;
    static const int MAX_HTTP_PORT = 65001;

    extern EnumSetting* http_enable;
    extern IntSetting*  http_port;

#    ifdef ENABLE_AUTHENTICATION
    struct AuthenticationIP {
        IPAddress           ip;
        AuthenticationLevel level;
        char                userID[17];
        char                sessionID[17];
        uint32_t            last_time;
        AuthenticationIP*   _next;
    };
#    endif

    //Upload status
    enum class UploadStatus : uint8_t { NONE = 0, FAILED = 1, CANCELLED = 2, SUCCESSFUL = 3, ONGOING = 4 };

    class Web_Server {
    public:
        Web_Server();

        bool begin();
        void end();
        void handle();

        static long     get_client_ID();
        static uint16_t port() { return _port; }

        ~Web_Server();

    private:
        static bool              _setupdone;
        static WebServer*        _webserver;
        static long              _id_connection;
        static WebSocketsServer* _socket_server;
        static uint16_t          _port;
        static UploadStatus      _upload_status;
        static String            _uploadFilename;
        static FileStream*       _uploadFile;
        static String            getContentType(String filename);
        static String            get_Splited_Value(String data, char separator, int index);

        static AuthenticationLevel is_authenticated();
#    ifdef ENABLE_AUTHENTICATION
        static AuthenticationIP*   _head;
        static uint8_t             _nb_ip;
        static bool                AddAuthIP(AuthenticationIP* item);
        static char*               create_session_ID();
        static bool                ClearAuthIP(IPAddress ip, const char* sessionID);
        static AuthenticationIP*   GetAuth(IPAddress ip, const char* sessionID);
        static AuthenticationLevel ResetAuthIP(IPAddress ip, const char* sessionID);
#    endif
        static void handle_SSDP();
        static void handle_root();
        static void handle_login();
        static void handle_not_found();
        static void _handle_web_command(bool);
        static void handle_web_command() { _handle_web_command(false); }
        static void handle_web_command_silent() { _handle_web_command(true); }
        static void handle_Websocket_Event(uint8_t num, uint8_t type, uint8_t* payload, size_t length);
        static void LocalFSFileupload();
        static void handleFileList();
        static void handleUpdate();
        static void WebUpdateUpload();
        static void pushError(int code, const char* st, bool web_error = 500, uint16_t timeout = 1000);
        static void cancelUpload();
        static void handle_direct_SDFileList();
        static void SDFile_direct_upload();
        static bool deleteFSRecursive(fs::FS& fs, String path);
        static bool deleteLocalFSRecursive(String path);
        static bool deleteRecursive(String path);
        static void uploadStart(String filename, size_t filesize, const char* fs);
        static void uploadWrite(uint8_t* buffer, size_t length);
        static void uploadEnd(size_t filesize, const char* fs);
        static void uploadStop();
        static void uploadCheck(String filename, const char* fs);
        static void deleteFile(const char* filename, const char* fs);

        static uint64_t fsAvail(const char* fs);
    };

    extern Web_Server web_server;
}

#endif
