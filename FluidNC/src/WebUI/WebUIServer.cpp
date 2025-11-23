// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Machine/MachineConfig.h"
#include "Serial.h"    // is_realtime_command()
#include "Settings.h"  // settings_execute_line()

#include "WebUIServer.h"

#include "Mdns.h"

#include <WiFi.h>
#include <StreamString.h>
#include <Update.h>
#include <esp_wifi_types.h>
#include <DNSServer.h>

#include "WSChannel.h"

#include "WebClient.h"

#include "Protocol.h"  // protocol_send_event
#include "FluidPath.h"
#include "JSONEncoder.h"

#include "HashFS.h"
#include <list>

#include "Mime.h"  // getContentType

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "WebDAV.h"

namespace WebUI {
    const byte DNS_PORT = 53;
    DNSServer  dnsServer;
}

using namespace asyncsrv;

#include <esp_ota_ops.h>

//embedded response file if no files on LocalFS
#include "NoFile.h"

namespace WebUI {
    // Error codes for upload
    const int ESP_ERROR_AUTHENTICATION   = 1;
    const int ESP_ERROR_FILE_CREATION    = 2;
    const int ESP_ERROR_FILE_WRITE       = 3;
    const int ESP_ERROR_UPLOAD           = 4;
    const int ESP_ERROR_NOT_ENOUGH_SPACE = 5;
    const int ESP_ERROR_UPLOAD_CANCELLED = 6;
    const int ESP_ERROR_FILE_CLOSE       = 7;

    static const char LOCATION_HEADER[] = "Location";

    bool     WebUI_Server::_setupdone            = false;
    uint16_t WebUI_Server::_port                 = 0;
    bool     WebUI_Server::_schedule_reboot      = false;
    uint32_t WebUI_Server::_schedule_reboot_time = 0;

    UploadStatus               WebUI_Server::_upload_status   = UploadStatus::NONE;
    AsyncWebServer*            WebUI_Server::_webserver       = NULL;
    AsyncWebServer*            WebUI_Server::_websocketserver = NULL;
    AsyncHeaderFreeMiddleware* WebUI_Server::_headerFilter    = NULL;
    AsyncWebSocket*            WebUI_Server::_socket_server   = NULL;
    std::string                WebUI_Server::current_session  = "";
#ifdef ENABLE_AUTHENTICATION
    AuthenticationIP* WebUI_Server::_head  = NULL;
    uint8_t           WebUI_Server::_nb_ip = 0;
    const int         MAX_AUTH_IP          = 10;
#endif
    FileStream* WebUI_Server::_uploadFile = nullptr;

    EnumSetting *http_enable, *http_block_during_motion;
    IntSetting*  http_port;

    WebUI_Server::~WebUI_Server() {
        deinit();
    }

    void WebUI_Server::init() {
        http_port   = new IntSetting("HTTP Port", WEBSET, WA, "ESP121", "HTTP/Port", DEFAULT_HTTP_PORT, MIN_HTTP_PORT, MAX_HTTP_PORT);
        http_enable = new EnumSetting("HTTP Enable", WEBSET, WA, "ESP120", "HTTP/Enable", DEFAULT_HTTP_STATE, &onoffOptions);
        http_block_during_motion = new EnumSetting("Block serving HTTP content during motion",
                                                   WEBSET,
                                                   WA,
                                                   NULL,
                                                   "HTTP/BlockDuringMotion",
                                                   DEFAULT_HTTP_BLOCKED_DURING_MOTION,
                                                   &onoffOptions);

        _setupdone = false;

        if (WiFi.getMode() == WIFI_OFF || !http_enable->get()) {
            return;
        }

        _port = http_port->get();

        //create instance
        _webserver    = new AsyncWebServer(_port);
        _headerFilter = new AsyncHeaderFreeMiddleware();

        //here the list of headers to be recorded
        _headerFilter->keep("Accept");
        _headerFilter->keep("Accept-Encoding");
        _headerFilter->keep("Cookie");
        _headerFilter->keep("If-None-Match");

        // WebDAV needs these
        _headerFilter->keep("Depth");
        _headerFilter->keep("Destination");

        //For websockets we need to keep these headers, otherwise this wouldn't work!
        _headerFilter->keep("Upgrade");
        _headerFilter->keep("Connection");
        _headerFilter->keep("Sec-WebSocket-Key");
        _headerFilter->keep("Sec-WebSocket-Version");
        _headerFilter->keep("Sec-WebSocket-Protocol");
        _headerFilter->keep("Sec-WebSocket-Extensions");

        _webserver->addMiddlewares({ _headerFilter });

        auto flash_dav = new WebDAV("/flash", localfsName);
        auto sd_dav    = new WebDAV("/sd", sdName);

        _webserver->addHandler(flash_dav);
        _webserver->addHandler(sd_dav);

        // The only major difference with websockets for v2 webui vs v3 seems to be the currentID vs CURRENT_ID and activeID vs ACTIVE_ID
        // In order to only have one websocket server (for simplicity and maintability reasons) we could:
        // 1 - Send both messages types all the time
        // 2 - Don't do anything and just send v3 payloads, since at this point it seems we don't rely on this pageId mechanism anymore with
        // our async and cookie session implementation
        // 3 - Remove all of these active and current IDs altogether since again, we may have no need for this anymore
        // 4 - Potentially check for a difference in requests headers of v2 vs v3 to dynamically send the proper payload in the same handler
        // For now, I've settled with #3
        _socket_server = new AsyncWebSocket("/");

        _socket_server->addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
            current_session = getSessionCookie(request);
            next();  // continue middleware chain
        });
        // Passing the current_session globally, lets hope there is no async switch back of other requests to change this in between
        _socket_server->onEvent(
            [](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
                WSChannels::handleEvent(server, client, type, arg, data, len, current_session);
            });

        _webserver->addHandler(_socket_server);

        //events functions
        //_web_events->onConnect(handle_onevent_connect);
        //events management
        // _webserver->addHandler(_web_events);

        //Web server handlers
        //trick to catch command line on "/" before file being processed
        _webserver->on("/", HTTP_ANY, handle_root);

        //Page not found handler
        _webserver->onNotFound(handle_not_found);

        //need to be there even no authentication to say to UI no authentication
        _webserver->on("/login", HTTP_ANY, handle_login);

        //web commands
        _webserver->on("/command", HTTP_ANY, handle_web_command);
        _webserver->on("/command_silent", HTTP_ANY, handle_web_command_silent);
        _webserver->on("/feedhold_reload", HTTP_ANY, handleFeedholdReload);
        _webserver->on("/cyclestart_reload", HTTP_ANY, handleCyclestartReload);
        _webserver->on("/restart_reload", HTTP_ANY, handleRestartReload);
        _webserver->on("/did_restart", HTTP_ANY, handleDidRestart);

        //LocalFS
        _webserver->on("/files", HTTP_ANY, handleFileList, LocalFSFileupload);

        //web update
        _webserver->on("/updatefw", HTTP_ANY, handleUpdate, WebUpdateUpload);

        //Direct SD management
        _webserver->on("/upload", HTTP_ANY, handle_direct_SDFileList, SDFileUpload);
        //_webserver->on("/SD", HTTP_ANY, handle_SDCARD);

        if (WiFi.getMode() == WIFI_AP) {
            // if DNSServer is started with "*" for domain name, it will reply with
            // provided IP to all DNS request
            dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
            log_info("Captive Portal Started");
            _webserver->on("/generate_204", HTTP_ANY, handle_root);
            _webserver->on("/gconnectivitycheck.gstatic.com", HTTP_ANY, handle_root);
            //do not forget the / at the end
            _webserver->on("/fwlink/", HTTP_ANY, handle_root);
        }

        log_info("HTTP started on port " << WebUI::http_port->get());
        //start webserver
        _webserver->begin();

        Mdns::add("_http", "_tcp", _port);

        HashFS::hash_all();

        _setupdone = true;
    }

    void WebUI_Server::deinit() {
        _setupdone = false;

        //        SSDP.end();

        Mdns::remove("_http", "_tcp");

        if (_socket_server) {
            delete _socket_server;
            _socket_server = NULL;
        }

        if (_webserver) {
            delete _webserver;
            _webserver = NULL;
        }

        if (_websocketserver) {
            delete _websocketserver;
            _websocketserver = NULL;
        }

        if (_headerFilter) {
            delete _headerFilter;
            _headerFilter = NULL;
        }

#ifdef ENABLE_AUTHENTICATION
        while (_head) {
            AuthenticationIP* current = _head;
            _head                     = _head->_next;
            delete current;
        }
        _nb_ip = 0;
#endif
    }

    std::string WebUI_Server::getSessionCookie(AsyncWebServerRequest* request) {
        if (request->hasHeader("Cookie")) {
            std::string cookies = request->getHeader("Cookie")->value().c_str();

            int pos = cookies.find("sessionId=");
            if (pos != std::string::npos) {
                int pos2 = cookies.find(";", pos);
                return cookies.substr(pos + strlen("sessionId="), pos2);
            }
        }
        return "";
    }

    static void get_random_string(char* str, unsigned int len) {
        unsigned int i;

        // reseed the random number generator
        srand(time(NULL));

        for (i = 0; i < len; i++) {
            // Add random printable ASCII char
            str[i] = (rand() % ('A' - 'Z')) + 'A';
        }
        str[i] = '\0';
    }
    // Send a file, either the specified path or path.gz
    bool WebUI_Server::myStreamFile(AsyncWebServerRequest* request, const char* path, bool download, bool setSession) {
        std::error_code ec;
        FluidPath       fpath { path, localfsName, ec };
        if (ec) {
            return false;
        }

        bool acceptGz = false;
        if (request->hasHeader("Accept-Encoding")) {
            auto encodings = std::string(request->getHeader("Accept-Encoding")->value().c_str());
            if (encodings.find("gzip") != std::string::npos) {
                acceptGz = true;
            }
        }

        std::string hash;

        // If you load or reload WebUI while a program is running, there is a high
        // risk of stalling the motion because serving a file from
        // the local FLASH filesystem takes away a lot of CPU cycles.  If we get
        // a request for a file when running, reject it to preserve the motion
        // integrity.
        // This can make it hard to debug ISR IRAM problems, because the easiest
        // way to trigger such problems is to refresh WebUI during motion.
        if (http_block_during_motion->get() && inMotionState()) {
            // Check to see if we have a cached hash of the file that can be retrieved without accessing FLASH
            hash = HashFS::hash(fpath, true);
            if (!hash.length() && acceptGz) {
                std::filesystem::path gzpath(fpath);
                gzpath += ".gz";
                hash = HashFS::hash(gzpath, true);
            }

            if (hash.length() && request->hasHeader("If-None-Match") &&
                std::string(request->getHeader("If-None-Match")->value().c_str()) == hash) {
                request->send(304);
                return true;
            }

            WebUI_Server::handleReloadBlocked(request);
            return true;
        }

        // Check for browser cache match
        hash = HashFS::hash(fpath);
        if (!hash.length() && acceptGz) {
            std::filesystem::path gzpath(fpath);
            gzpath += ".gz";
            hash = HashFS::hash(gzpath);
        }
        if (hash.length() && request->hasHeader("If-None-Match") &&
            std::string(request->getHeader("If-None-Match")->value().c_str()) == hash) {
            if (setSession && getSessionCookie(request) == "") {
                char session[9];
                get_random_string(session, sizeof(session) - 1);
                AsyncWebServerResponse* response = request->beginResponse(304);
                response->addHeader("Set-Cookie", ("sessionId=" + std::string(session)).c_str());
                request->send(response);
            } else {
                request->send(304);
            }
            return true;
        }

        bool        isGzip = false;
        FileStream* file   = NULL;
        try {
            file = new FileStream(path, "r", "");
        } catch (const Error err) {
            if (acceptGz) {
                try {
                    std::string gzpath(fpath);
                    //                    std::filesystem::path gzpath(fpath);
                    gzpath += ".gz";
                    file   = new FileStream(gzpath, "r", "");
                    isGzip = true;
                } catch (const Error err) {}
            }
        }
        if (!file) {
            log_debug(path << " not found");
            return false;
        }

        AsyncWebServerResponse* response = request->beginResponse(
            getContentType(path), file->size(), [file, request](uint8_t* buffer, size_t maxLen, size_t total) mutable -> size_t {
                if (!file) {
                    request->client()->close();
                    return 0;  //RESPONSE_TRY_AGAIN; // This only works for ChunkedResponse
                }
                if (total >= file->size() || request->methodToString() != "GET") {
                    file = nullptr;
                    return 0;
                }
                size_t bytes  = min(file->size(), maxLen);
                int    actual = file->read(buffer, bytes);  // return 0 even when no bytes were loaded
                if (actual == 0 || (actual + total) >= file->size()) {
                    file = nullptr;
                }
                return bytes;
            });

        request->onDisconnect([request, file]() { delete file; });

        if (setSession && getSessionCookie(request) == "") {
            char session[9];
            get_random_string(session, sizeof(session) - 1);
            response->addHeader("Set-Cookie", ("sessionId=" + std::string(session)).c_str());
        }
        if (download) {
            response->addHeader("Content-Disposition", "attachment");
        }
        if (hash.length()) {
            response->addHeader("ETag", hash.c_str());
        }
        // content length is set automatically
        // response->setContentLength(file->size());
        if (isGzip) {
            response->addHeader(T_Content_Encoding, T_gzip);
        }
        request->send(response);

        return true;
    }
    void WebUI_Server::sendWithOurAddress(AsyncWebServerRequest* request, const char* content, uint16_t code) {
        auto        ip    = WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP();
        std::string ipstr = IP_string(ip);
        if (_port != 80) {
            ipstr += ":";
            ipstr += std::to_string(_port);
        }

        std::string scontent(content);
        replace_string_in_place(scontent, "$WEB_ADDRESS$", ipstr);
        replace_string_in_place(scontent, "$QUERY$", request->url().c_str());
        request->send(code, "text/html", scontent.c_str());
    }

    // Captive Portal Page for use in AP mode
    const char PAGE_CAPTIVE[] =
        "<HTML>\n<HEAD>\n<title>Captive Portal</title> \n</HEAD>\n<BODY>\n<CENTER>Captive Portal page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void WebUI_Server::sendCaptivePortal(AsyncWebServerRequest* request) {
        sendWithOurAddress(request, PAGE_CAPTIVE, 200);
    }

    //Default 404 page that is sent when a request cannot be satisfied
    const char PAGE_404[] =
        "<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void WebUI_Server::send404Page(AsyncWebServerRequest* request) {
        sendWithOurAddress(request, PAGE_404, 404);
    }

    void WebUI_Server::handle_root(AsyncWebServerRequest* request) {
        log_info("WebUI: Request from " << request->client()->remoteIP());
        if (!(request->hasParam("forcefallback") && request->getParam("forcefallback")->value() == "yes")) {
            if (myStreamFile(request, "index.html", false, true)) {
                return;
            }
        }

        // If we did not send index.html, send the default content that provides simple localfs file management
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html", (const uint8_t*)PAGE_NOFILES, PAGE_NOFILES_SIZE);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    }

    // Handle filenames and other things that are not explicitly registered
    void WebUI_Server::handle_not_found(AsyncWebServerRequest* request) {
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            request->redirect("/");
            //_webserver->client().stop();
            return;
        }

        std::string path(request->url().c_str());  //request->urlDecode(request->url()).c_str());

        if (path.rfind("/api/", 0) == 0) {
            request->send(404);
            return;
        }

        // Download a file.  The true forces a download instead of displaying the file
        if (myStreamFile(request, path.c_str(), true)) {
            return;
        }

        if (WiFi.getMode() == WIFI_AP) {
            sendCaptivePortal(request);
            return;
        }

        // This lets the user customize the not-found page by
        // putting a "404.htm" file on the local filesystem
        if (myStreamFile(request, "404.htm")) {
            return;
        }

        send404Page(request);
    }

    // WebUI sends a PAGEID arg to identify the websocket it is using
    uint32_t WebUI_Server::getPageid(AsyncWebServerRequest* request) {
        if (request->hasParam("PAGEID")) {
            return request->getParam("PAGEID")->value().toInt();
        }
        return 0;  // ID 0 means none
    }

    void WebUI_Server::synchronousCommand(
        AsyncWebServerRequest* request, const char* cmd, bool silent, AuthenticationLevel auth_level, bool allowedInMotion) {
        // Can we do this with async?
        if (http_block_during_motion->get() && inMotionState() && !allowedInMotion) {  // ESP800 is to allow a cached paged reload on webui3
            request->send(503, "text/plain", "Try again when not moving\n");
            return;
        }
        char line[256];
        strncpy(line, cmd, 255);
        AsyncWebServerResponse* response;
        if (request->methodToString() == "GET") {
            WebClient* webClient = new WebClient();
            webClient->attachWS(silent);
            webClient->executeCommandBackground(line);
            response = request->beginChunkedResponse("", [webClient, request](uint8_t* buffer, size_t maxLen, size_t total) mutable -> size_t {
                // The method can change before the end... not good
                //if(request->methodToString() != "GET")
                //    return 0;
                auto ret = webClient->copyBufferSafe(buffer, min((int)maxLen, 1024), total);
                return ret;
            });
            // onDisconnect MUST always happen, otherwise commands being processed will wait indefinitly to be read
            // by the callback that may never happen if the client is dead
            // We rely on AsyncWebServer to take care of that
            request->onDisconnect([webClient]() {
                webClient->detachWS();
                allChannels.kill(webClient);
                // Should not delete, kill() takes care of that
                //delete webClient;
            });
        } else
            response = request->beginResponse(200, "", "");
        response->addHeader(T_Cache_Control, T_no_cache);
        request->send(response);
        return;
    }

    std::string getSession(AsyncClient* client) {
        return (std::to_string(IPAddress(client->getRemoteAddress())) + ":" + std::to_string(client->getRemotePort()));
    }
    void WebUI_Server::websocketCommand(AsyncWebServerRequest* request, const char* cmd, uint32_t pageid, AuthenticationLevel auth_level) {
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            request->send(401, "text/plain", "Authentication failed\n");
            return;
        }
        std::string session  = getSessionCookie(request);
        bool        hasError = WSChannels::runGCode(pageid, cmd, session);
        request->send(hasError ? 500 : 200, "text/plain", hasError ? "WebSocket dead" : "");
    }

    bool WebUI_Server::isAllowedInMotion(String cmd) {
        if (cmd.startsWith("[ESP800]"))
            return true;

        return false;
    }
    void WebUI_Server::_handle_web_command(AsyncWebServerRequest* request, bool silent) {
        AuthenticationLevel auth_level = is_authenticated();
        if (request->hasParam("cmd") || request->hasParam("commandText")) {
            String cmd;
            if (request->hasParam("cmd"))
                cmd = request->getParam("cmd")->value();
            else
                cmd = request->getParam("commandText")->value();
            // [ESPXXX] commands expect data in the HTTP response
            String cmdUpper = cmd;
            cmdUpper.toUpperCase();
            // Modified async hack // no longer needed...
            //if (cmdUpper.startsWith("[ESP") || cmdUpper.startsWith("$/") || cmdUpper.startsWith("$ESP") {
            // Original check (now also work with $ESP400, but is slower than if it was returned as http response)
            if (cmdUpper.startsWith("[ESP") || cmdUpper.startsWith("$/")) {
                synchronousCommand(request, cmd.c_str(), silent, auth_level, isAllowedInMotion(cmdUpper));
            } else {
                websocketCommand(request, cmd.c_str(), getPageid(request), auth_level);
            }
            return;
        }
        if (request->hasParam("plain")) {
            synchronousCommand(request, request->getParam("plain")->value().c_str(), silent, auth_level);
            return;
        }
        request->send(500, "text/plain", "Invalid command");
    }

    //login status check
    void WebUI_Server::handle_login(AsyncWebServerRequest* request) {
#ifdef ENABLE_AUTHENTICATION
        const char* smsg;
        std::string sUser, sPassword;
        const char* auths;
        uint16_t    code            = 200;
        bool        msg_alert_error = false;
        //disconnect can be done anytime no need to check credential
        if (_webserver->hasArg("DISCONNECT")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            std::string sessionID;
            if (pos != std::string::npos) {
                size_t pos2 = cookie.find(";", pos);
                sessionID   = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
            }
            ClearAuthIP(_webserver->client().remoteIP(), sessionID);
            _webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0");
            _webserver->sendHeader(T_Cache_Control, T_no_cache);
            sendAuth("Ok", "guest", "");
            //_webserver->client().stop();
            return;
        }

        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            auths = "guest";
        } else if (auth_level == AuthenticationLevel::LEVEL_USER) {
            auths = "user";
        } else if (auth_level == AuthenticationLevel::LEVEL_ADMIN) {
            auths = "admin";
        } else {
            auths = "???";
        }

        //check is it is a submission or a query
        if (_webserver->hasArg("SUBMIT")) {
            //is there a correct list of query?
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER")) {
                //USER
                sUser = _webserver->arg("USER").c_str();
                if (!((sUser == DEFAULT_ADMIN_LOGIN) || (sUser == DEFAULT_USER_LOGIN))) {
                    msg_alert_error = true;
                    smsg            = "Error : Incorrect User";
                    code            = 401;
                }

                if (msg_alert_error == false) {
                    //Password
                    sPassword = _webserver->arg("PASSWORD").c_str();
                    std::string sadminPassword(admin_password->get());
                    std::string suserPassword(user_password->get());

                    if (!(sUser == DEFAULT_ADMIN_LOGIN && sPassword == sadminPassword) ||
                        (sUser == DEFAULT_USER_LOGIN && sPassword == suserPassword)) {
                        msg_alert_error = true;
                        smsg            = "Error: Incorrect password";
                        code            = 401;
                    }
                }
            } else {
                msg_alert_error = true;
                smsg            = "Error: Missing data";
                code            = 500;
            }
            //change password
            if (_webserver->hasArg("PASSWORD") && _webserver->hasArg("USER") && _webserver->hasArg("NEWPASSWORD") &&
                (msg_alert_error == false)) {
                std::string newpassword(_webserver->arg("NEWPASSWORD").c_str());

                char pwdbuf[MAX_LOCAL_PASSWORD_LENGTH + 1];
                newpassword.toCharArray(pwdbuf, MAX_LOCAL_PASSWORD_LENGTH + 1);

                Error err;

                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    err = admin_password->setStringValue(pwdbuf);
                } else {
                    err = user_password->setStringValue(pwdbuf);
                }
                if (err != Error::Ok) {
                    msg_alert_error = true;
                    smsg            = "Error: Password cannot contain spaces";
                    code            = 500;
                }
            }
            if ((code == 200) || (code == 500)) {
                AuthenticationLevel current_auth_level;
                if (sUser == DEFAULT_ADMIN_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_ADMIN;
                } else if (sUser == DEFAULT_USER_LOGIN) {
                    current_auth_level = AuthenticationLevel::LEVEL_USER;
                } else {
                    current_auth_level = AuthenticationLevel::LEVEL_GUEST;
                }
                //create Session
                if ((current_auth_level != auth_level) || (auth_level == AuthenticationLevel::LEVEL_GUEST)) {
                    AuthenticationIP* current_auth = new AuthenticationIP;
                    current_auth->level            = current_auth_level;
                    current_auth->ip               = _webserver->client().remoteIP();
                    strcpy(current_auth->sessionID, create_session_ID());
                    strcpy(current_auth->userID, sUser.c_str());
                    current_auth->last_time = millis();
                    if (AddAuthIP(current_auth)) {
                        std::string tmps = "ESPSESSIONID=";
                        tmps += current_auth->sessionID.c_str();
                        _webserver->sendHeader("Set-Cookie", tmps);
                        _webserver->sendHeader(T_Cache_Control, T_no_cache);
                        switch (current_auth->level) {
                            case AuthenticationLevel::LEVEL_ADMIN:
                                auths = "admin";
                                break;
                            case AuthenticationLevel::LEVEL_USER:
                                auths = "user";
                                break;
                            default:
                                auths = "guest";
                                break;
                        }
                    } else {
                        delete current_auth;
                        msg_alert_error = true;
                        code            = 500;
                        smsg            = "Error: Too many connections";
                    }
                }
            }
            if (code == 200) {
                smsg = "Ok";
            }

            sendAuth("Ok", "guest", "");
        } else {
            if (auth_level != AuthenticationLevel::LEVEL_GUEST) {
                std::string cookie(_webserver->header("Cookie").c_str());
                size_t      pos = cookie.find("ESPSESSIONID=");
                std::string sessionID;
                if (pos != std::string::npos) {
                    size_t pos2                         = cookie.find(";", pos);
                    sessionID                           = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                    AuthenticationIP* current_auth_info = GetAuth(_webserver->client().remoteIP(), sessionID.c_str());
                    if (current_auth_info != NULL) {
                        sUser = current_auth_info->userID;
                    }
                }
            }
            sendAuth(smsg, auths, "");
        }
#else
        sendAuth(request, "Ok", "admin", "");
#endif
    }

    // This page is used when you try to reload WebUI during motion,
    // to avoid interrupting that motion.  It lets you wait until
    // motion is finished.
    void WebUI_Server::handleReloadBlocked(AsyncWebServerRequest* request) {
        request->send(503,
                      "text/html",
                      "<!DOCTYPE html><html><body>"
                      "<h3>Cannot load WebUI while GCode Program is Running</h3>"

                      "<button onclick='window.location.replace(\"/feedhold_reload\")'>Pause</button>"
                      "&nbsp;Pause the GCode program with feedhold<br><br>"

                      "<button onclick='window.location.replace(\"/restart_reload\")'>Stop</button>"
                      "&nbsp;Stop the GCode Program with reset<br><br>"

                      "<button onclick='window.location.reload()'>Reload WebUI</button>"
                      "&nbsp;(You must first stop the GCode program or wait for it to finish)<br><br>"

                      "</body></html>");
    }
    void WebUI_Server::handleDidRestart(AsyncWebServerRequest* request) {
        request->send(503,
                      "text/html",
                      "<!DOCTYPE html><html><body>"
                      "<h3>GCode Program has been stopped</h3>"
                      "<button onclick='window.location.replace(\"/\")'>Reload WebUI</button>"
                      "</body></html>");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleFeedholdReload(AsyncWebServerRequest* request) {
        protocol_send_event(&feedHoldEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleCyclestartReload(AsyncWebServerRequest* request) {
        protocol_send_event(&cycleStartEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/");
    }
    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void WebUI_Server::handleRestartReload(AsyncWebServerRequest* request) {
        protocol_send_event(&rtResetEvent);
        //        delay(100);
        //        delay(100);
        // Go to the main page
        request->redirect("/did_restart");
    }

    // push error code and message to websocket.  Used by upload code
    void WebUI_Server::pushError(AsyncWebServerRequest* request, uint16_t code, const char* st, int32_t web_error, uint16_t timeout) {
        if (_socket_server && st) {
            std::string s("ERROR:");
            s += std::to_string(code) + ":";
            s += st;

            WSChannels::sendError(getPageid(request), st, getSessionCookie(request));

            if (web_error != 0 && request) {
                request->send(web_error, "text/xml", st);
            }
        }
    }

    //abort reception of packages
    void WebUI_Server::cancelUpload(AsyncWebServerRequest* request) {
        request->client()->close();
        delay(100);
    }

    //LocalFS files uploader handle
    void WebUI_Server::fileUpload(
        AsyncWebServerRequest* request, const char* fs, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (!index) {
            std::string sizeargname(filename.c_str());
            sizeargname += "S";
            size_t filesize = request->hasParam(sizeargname.c_str()) ? request->getParam(sizeargname.c_str())->value().toInt() : 0;
            uploadStart(request, filename.c_str(), filesize, fs);
        }
        if (_upload_status == UploadStatus::ONGOING) {
            uploadWrite(request, data, len);
            if (final) {
                std::string sizeargname(filename.c_str());
                sizeargname += "S";
                size_t filesize = request->hasParam(sizeargname.c_str()) ? request->getParam(sizeargname.c_str())->value().toInt() : 0;
                uploadEnd(request, filesize);
            }
        } else {
            uploadStop();
            return;
        }

        uploadCheck(request);

        return;
    }

    void WebUI_Server::sendJSON(AsyncWebServerRequest* request, uint16_t code, const char* s) {
        AsyncWebServerResponse* response = request->beginResponse(code, T_application_json, s);
        response->addHeader(T_Cache_Control, T_no_cache);
        request->send(response);
    }

    void WebUI_Server::sendAuth(AsyncWebServerRequest* request, const char* status, const char* level, const char* user) {
        AsyncResponseStream* response = request->beginResponseStream(T_application_json);
        response->setCode(200);
        response->addHeader(T_Cache_Control, T_no_cache);

        JSONencoder j([response](const char* s) { response->print(s); });
        j.begin();
        j.member("status", status);
        if (*level != '\0') {
            j.member("authentication_lvl", level);
        }
        if (*user != '\0') {
            j.member("user", user);
        }
        j.end();
        request->send(response);
    }

    void WebUI_Server::sendStatus(AsyncWebServerRequest* request, uint16_t code, const char* status) {
        AsyncResponseStream* response = request->beginResponseStream(T_application_json);
        response->setCode(code);
        response->addHeader(T_Cache_Control, T_no_cache);

        JSONencoder j([response](const char* s) { response->print(s); });
        j.begin();
        j.member("status", status);
        j.end();
        request->send(response);
    }

    void WebUI_Server::sendAuthFailed(AsyncWebServerRequest* request) {
        sendStatus(request, 401, "Authentication failed");
    }

    void WebUI_Server::LocalFSFileupload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        return fileUpload(request, localfsName, filename, index, data, len, final);
    }
    void WebUI_Server::SDFileUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        return fileUpload(request, sdName, filename, index, data, len, final);
    }

    //Web Update handler
    void WebUI_Server::handleUpdate(AsyncWebServerRequest* request) {
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level != AuthenticationLevel::LEVEL_ADMIN) {
            _upload_status = UploadStatus::NONE;
            request->send(403, "text/plain", "Not allowed, log in first!\n");
            return;
        }

        //if success restart
        if (_upload_status == UploadStatus::SUCCESSFUL) {
            sendStatus(request, 200, std::to_string(int(_upload_status)).c_str());
            _schedule_reboot_time = millis() + 1000;
            _schedule_reboot      = true;
        } else {
            sendStatus(request, 200, std::to_string(int(_upload_status)).c_str());
            _upload_status = UploadStatus::NONE;
        }
    }

    //File upload for Web update
    void WebUI_Server::WebUpdateUpload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
        static size_t   last_upload_update;
        static uint32_t maxSketchSpace = UINT32_MAX;

        //only admin can update FW
        if (is_authenticated() != AuthenticationLevel::LEVEL_ADMIN) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload rejected");
            sendAuthFailed(request);
            //pushError(request, ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
        } else {
            //Upload start
            //**************
            if (!index) {  //upload.status == UPLOAD_FILE_START) {
                log_info("Update Firmware");
                _upload_status = UploadStatus::ONGOING;
                std::string sizeargname(filename.c_str());
                sizeargname += "S";
                if (request->hasParam(sizeargname.c_str()))
                    maxSketchSpace = request->getParam(sizeargname.c_str())->value().toInt();
                else if (request->hasHeader("Content-Length"))
                    maxSketchSpace = request->getHeader("Content-Length")->value().toInt();
                //check space
                size_t flashsize = 0;
                if (esp_ota_get_running_partition()) {
                    const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                    if (partition) {
                        flashsize = partition->size;
                    }
                }
                if (flashsize < maxSketchSpace) {
                    String msg = String("Upload rejected, not enough space (needs " + String(maxSketchSpace) + ", has " + String(flashsize));
                    pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, msg.c_str());
                    _upload_status = UploadStatus::FAILED;
                    log_info("Update cancelled");
                }
                if (_upload_status != UploadStatus::FAILED) {
                    last_upload_update = 0;
                    if (!Update.begin()) {  //start with max available size
                        _upload_status = UploadStatus::FAILED;
                        log_info("Update cancelled");
                        pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                    } else {
                        log_info("Update 0%");
                    }
                }
            }
            //Upload write
            //**************
            //check if no error
            if (_upload_status == UploadStatus::ONGOING) {
                if (((100 * index) / maxSketchSpace) != last_upload_update) {
                    if (maxSketchSpace > 0) {
                        last_upload_update = (100 * index) / maxSketchSpace;
                    } else {
                        last_upload_update = index;
                    }

                    log_info("Update " << last_upload_update << "%");
                }
                if (Update.write(data, len) != len) {
                    _upload_status = UploadStatus::FAILED;
                    log_info("Update write failed");
                    pushError(request, ESP_ERROR_FILE_WRITE, "File write failed");
                }
            }
            //Upload end
            //**************
            if (final) {
                if (_upload_status == UploadStatus::ONGOING && Update.end(true)) {  //true to set the size to the current progress
                    //Now Reboot
                    log_info("Update 100%");
                    _upload_status = UploadStatus::SUCCESSFUL;
                } else {
                    _upload_status = UploadStatus::FAILED;
                    log_info("Update failed");
                    pushError(request, ESP_ERROR_UPLOAD, "Update upload failed");
                }
            }
        }
    }

    void WebUI_Server::handleFileOps(AsyncWebServerRequest* request, const char* fs) {
        //this is only for admin and user
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _upload_status = UploadStatus::NONE;
            sendAuthFailed(request);
            return;
        }

        std::error_code ec;

        std::string path("");
        std::string sstatus("Ok");
        if ((_upload_status == UploadStatus::FAILED) || (_upload_status == UploadStatus::FAILED)) {
            sstatus = "Upload failed";
        }
        _upload_status      = UploadStatus::NONE;
        bool     list_files = true;
        uint64_t totalspace = 0;
        uint64_t usedspace  = 0;

        //get current path
        if (request->hasParam("path")) {
            path += request->getParam("path")->value().c_str();
            // path.trim();
            replace_string_in_place(path, "//", "/");
            if (path[path.length() - 1] == '/') {
                path = path.substr(0, path.length() - 1);
            }
            if (path.length() && path[0] == '/') {
                path = path.substr(1);
            }
        }

        FluidPath fpath { path, fs, ec };
        if (ec) {
            sendJSON(request, 200, "{\"status\":\"No SD card\"}");
            return;
        }

        // Handle deletions and directory creation
        if (request->hasParam("action") && request->hasParam("filename")) {
            std::string action(request->getParam("action")->value().c_str());
            std::string filename = std::string(request->getParam("filename")->value().c_str());
            if (action == "delete") {
                if (stdfs::remove(fpath / filename, ec)) {
                    sstatus = filename + " deleted";
                    HashFS::delete_file(fpath / filename);
                } else {
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "deletedir") {
                stdfs::path dirpath { fpath / filename };
                log_debug("Deleting directory " << dirpath.string().c_str());
                size_t count = stdfs::remove_all(dirpath, ec);
                if (count > 0) {
                    sstatus = filename + " deleted";
                    HashFS::report_change();
                } else {
                    log_debug("remove_all returned " << count);
                    sstatus = "Cannot delete ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "createdir") {
                if (stdfs::create_directory(fpath / filename, ec)) {
                    sstatus = filename + " created";
                    HashFS::report_change();
                } else {
                    sstatus = "Cannot create ";
                    sstatus += filename + " " + ec.message();
                }
            } else if (action == "rename") {
                if (!request->hasParam("newname")) {
                    sstatus = "Missing new filename";
                } else {
                    std::string newname = std::string(request->getParam("newname")->value().c_str());
                    std::filesystem::rename(fpath / filename, fpath / newname, ec);
                    if (ec) {
                        sstatus = "Cannot rename ";
                        sstatus += filename + " " + ec.message();
                    } else {
                        sstatus = filename + " renamed to " + newname;
                        HashFS::rename_file(fpath / filename, fpath / newname);
                    }
                }
            }
        }

        //check if no need build file list
        if (request->hasParam("dontlist") && request->getParam("dontlist")->value() == "yes") {
            list_files = false;
        }

        AsyncResponseStream* response = request->beginResponseStream(T_application_json);
        response->setCode(200);
        response->addHeader(T_Cache_Control, T_no_cache);

        JSONencoder j([response](const char* s) { response->print(s); });

        j.begin();

        if (list_files) {
            auto iter = stdfs::directory_iterator { fpath, ec };
            if (!ec) {
                j.begin_array("files");
                for (auto const& dir_entry : iter) {
                    j.begin_object();
                    j.member("name", dir_entry.path().filename().string());
                    j.member("shortname", dir_entry.path().filename().string());
                    j.member("size", dir_entry.is_directory() ? -1 : dir_entry.file_size());
                    j.member("datetime", "");
                    j.end_object();
                }
                j.end_array();
            }
        }

        auto space = stdfs::space(fpath, ec);
        totalspace = space.capacity;
        usedspace  = totalspace - space.available;

        j.member("path", path.c_str());
        j.member("total", formatBytes(totalspace));
        j.member("used", formatBytes(usedspace + 1));

        uint8_t percent = totalspace ? (usedspace * 100) / totalspace : 100;

        j.member("occupation", percent);
        j.member("status", sstatus);
        j.end();

        request->send(response);
    }

    void WebUI_Server::handle_direct_SDFileList(AsyncWebServerRequest* request) {
        handleFileOps(request, sdName);
    }
    void WebUI_Server::handleFileList(AsyncWebServerRequest* request) {
        handleFileOps(request, localfsName);
    }

    // File upload
    void WebUI_Server::uploadStart(AsyncWebServerRequest* request, const char* filename, size_t filesize, const char* fs) {
        std::error_code ec;

        FluidPath fpath { filename, fs, ec };
        if (ec) {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload filesystem inaccessible");
            pushError(request, ESP_ERROR_FILE_CREATION, "Upload rejected, filesystem inaccessible");
            return;
        }

        auto space = stdfs::space(fpath);
        if (filesize && filesize > space.available) {
            // If the file already exists, maybe there will be enough space
            // when we replace it.
            auto existing_size = stdfs::file_size(fpath, ec);
            if (ec || (filesize > (space.available + existing_size))) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload not enough space");
                pushError(request, ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                return;
            }
        }

        if (_upload_status != UploadStatus::FAILED) {
            //Create file for writing
            try {
                _uploadFile    = new FileStream(fpath, "w");
                _upload_status = UploadStatus::ONGOING;
            } catch (const Error err) {
                _uploadFile    = nullptr;
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - cannot create file");
                pushError(request, ESP_ERROR_FILE_CREATION, "File creation failed");
            }
        }
    }

    void WebUI_Server::uploadWrite(AsyncWebServerRequest* request, uint8_t* buffer, size_t length) {
        delay_ms(1);
        if (_uploadFile && _upload_status == UploadStatus::ONGOING) {
            //no error write post data
            if (length != _uploadFile->write(buffer, length)) {
                _upload_status = UploadStatus::FAILED;
                log_info("Upload failed - file write failed");
                pushError(request, ESP_ERROR_FILE_WRITE, "File write failed");
            }
        } else {  //if error set flag UploadStatus::FAILED
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(request, ESP_ERROR_FILE_WRITE, "File not open");
        }
    }

    void WebUI_Server::uploadEnd(AsyncWebServerRequest* request, size_t filesize) {
        //if file is open close it
        if (_uploadFile) {
            //            delete _uploadFile;
            // _uploadFile = nullptr;

            std::string pathname = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            log_debug("pathname " << pathname);

            FluidPath filepath { pathname, "" };

            HashFS::rehash_file(filepath);

            // Check size
            if (filesize) {
                size_t actual_size;
                try {
                    actual_size = stdfs::file_size(filepath);
                } catch (const Error err) { actual_size = 0; }

                if (filesize != actual_size) {
                    _upload_status = UploadStatus::FAILED;
                    pushError(request, ESP_ERROR_UPLOAD, "File upload mismatch");
                    log_info("Upload failed - size mismatch - exp " << filesize << " got " << actual_size);
                }
            }
        } else {
            _upload_status = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(request, ESP_ERROR_FILE_CLOSE, "File close failed");
        }
        if (_upload_status == UploadStatus::ONGOING) {
            _upload_status = UploadStatus::SUCCESSFUL;
        } else {
            _upload_status = UploadStatus::FAILED;
            pushError(request, ESP_ERROR_UPLOAD, "Upload error 8");
        }
    }
    void WebUI_Server::uploadStop() {
        _upload_status = UploadStatus::FAILED;
        if (_uploadFile) {
            log_info("Upload cancelled");
            std::filesystem::path filepath = _uploadFile->fpath();
            delete _uploadFile;
            _uploadFile = nullptr;
            HashFS::rehash_file(filepath);
        }
    }
    void WebUI_Server::uploadCheck(AsyncWebServerRequest* request) {
        std::error_code error_code;
        if (_upload_status == UploadStatus::FAILED) {
            cancelUpload(request);
            if (_uploadFile) {
                std::filesystem::path filepath = _uploadFile->fpath();
                delete _uploadFile;
                _uploadFile = nullptr;
                stdfs::remove(filepath, error_code);
                HashFS::rehash_file(filepath);
            }
        }
    }

    void WebUI_Server::poll() {
        static uint32_t start_time = millis();
        if (WiFi.getMode() == WIFI_AP) {
            dnsServer.processNextRequest();
        }
        if (_schedule_reboot and _schedule_reboot_time == millis()) {
            _schedule_reboot = false;
            protocol_send_event(&fullResetEvent);
        }
        if ((millis() - start_time) > 10000) {
            uint32_t heapsize = xPortGetFreeHeapSize();
            log_verbose("memory: " << heapsize << " min: " << heapLowWater);
            if (_socket_server) {
                _socket_server->cleanupClients();
                WSChannels::sendPing();
            }
            start_time = millis();
        }
    }

    //check authentication
    AuthenticationLevel WebUI_Server::is_authenticated() {
#ifdef ENABLE_AUTHENTICATION
        if (_webserver->hasHeader("Cookie")) {
            std::string cookie(_webserver->header("Cookie").c_str());
            size_t      pos = cookie.find("ESPSESSIONID=");
            if (pos != std::string::npos) {
                size_t      pos2      = cookie.find(";", pos);
                std::string sessionID = cookie.substr(pos + strlen("ESPSESSIONID="), pos2);
                IPAddress   ip        = _webserver->client().remoteIP();
                //check if cookie can be reset and clean table in same time
                return ResetAuthIP(ip, sessionID.c_str());
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
#else
        return AuthenticationLevel::LEVEL_ADMIN;
#endif
    }

#ifdef ENABLE_AUTHENTICATION

    //add the information in the linked list if possible
    bool WebUI_Server::AddAuthIP(AuthenticationIP* item) {
        if (_nb_ip > MAX_AUTH_IP) {
            return false;
        }
        item->_next = _head;
        _head       = item;
        _nb_ip++;
        return true;
    }

    //Session ID based on IP and time using 16 char
    const char* WebUI_Server::create_session_ID() {
        static char sessionID[17];
        //reset SESSIONID
        for (size_t i = 0; i < 17; i++) {
            sessionID[i] = '\0';
        }
        //get time
        uint32_t now = millis();
        //get remote IP
        IPAddress remoteIP = _webserver->client().remoteIP();
        //generate SESSIONID
        if (0 > sprintf(sessionID,
                        "%02X%02X%02X%02X%02X%02X%02X%02X",
                        remoteIP[0],
                        remoteIP[1],
                        remoteIP[2],
                        remoteIP[3],
                        (uint8_t)((now >> 0) & 0xff),
                        (uint8_t)((now >> 8) & 0xff),
                        (uint8_t)((now >> 16) & 0xff),
                        (uint8_t)((now >> 24) & 0xff))) {
            strcpy(sessionID, "NONE");
        }
        return sessionID;
    }

    bool WebUI_Server::ClearAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        bool              done     = false;
        while (current) {
            if ((ip == current->ip) && (strcmp(sessionID, current->sessionID) == 0)) {
                //remove
                done = true;
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                previous = current;
                current  = current->_next;
            }
        }
        return done;
    }

    //Get info
    AuthenticationIP* WebUI_Server::GetAuth(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current = _head;
        //AuthenticationIP * previous = NULL;
        while (current) {
            if (ip == current->ip) {
                if (strcmp(sessionID, current->sessionID) == 0) {
                    //found
                    return current;
                }
            }
            //previous = current;
            current = current->_next;
        }
        return NULL;
    }

    //Review all IP to reset timers
    AuthenticationLevel WebUI_Server::ResetAuthIP(IPAddress ip, const char* sessionID) {
        AuthenticationIP* current  = _head;
        AuthenticationIP* previous = NULL;
        while (current) {
            if ((millis() - current->last_time) > 360000) {
                //remove
                if (current == _head) {
                    _head = current->_next;
                    _nb_ip--;
                    delete current;
                    current = _head;
                } else {
                    previous->_next = current->_next;
                    _nb_ip--;
                    delete current;
                    current = previous->_next;
                }
            } else {
                if (ip == current->ip && strcmp(sessionID, current->sessionID) == 0) {
                    //reset time
                    current->last_time = millis();
                    return (AuthenticationLevel)current->level;
                }
                previous = current;
                current  = current->_next;
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
    }
#endif

    ModuleFactory::InstanceBuilder<WebUI_Server> __attribute__((init_priority(108))) webui_server_module("webuiserver", true);
}
