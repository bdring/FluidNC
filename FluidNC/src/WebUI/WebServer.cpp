// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "../Machine/MachineConfig.h"
#include "../Config.h"
#include "../Serial.h"    // is_realtime_command()
#include "../Settings.h"  // settings_execute_line()

#ifdef ENABLE_WIFI

#    include "WifiServices.h"
#    include "WifiConfig.h"  // wifi_config

#    include "Serial2Socket.h"
#    include "WebServer.h"

#    include <WebSocketsServer.h>
#    include <WiFi.h>
#    include <WebServer.h>
#    include <ESP32SSDP.h>
#    include <StreamString.h>
#    include <Update.h>
#    include <esp_wifi_types.h>
#    include <ESPmDNS.h>
#    include <ESP32SSDP.h>
#    include <DNSServer.h>
#    include "WebSettings.h"

#    include "WebClient.h"

namespace WebUI {
    const byte DNS_PORT = 53;
    DNSServer  dnsServer;
}

#    include <esp_ota_ops.h>

//embedded response file if no files on LocalFS
#    include "NoFile.h"

namespace WebUI {

    // Error codes for upload
    const int ESP_ERROR_AUTHENTICATION   = 1;
    const int ESP_ERROR_FILE_CREATION    = 2;
    const int ESP_ERROR_FILE_WRITE       = 3;
    const int ESP_ERROR_UPLOAD           = 4;
    const int ESP_ERROR_NOT_ENOUGH_SPACE = 5;
    const int ESP_ERROR_UPLOAD_CANCELLED = 6;
    const int ESP_ERROR_FILE_CLOSE       = 7;

    Web_Server        webServer;
    bool              Web_Server::_setupdone     = false;
    uint16_t          Web_Server::_port          = 0;
    long              Web_Server::_id_connection = 0;
    UploadStatus      Web_Server::_uploadStatus  = UploadStatus::NONE;
    WebServer*        Web_Server::_webserver     = NULL;
    WebSocketsServer* Web_Server::_socket_server = NULL;
#    ifdef ENABLE_AUTHENTICATION
    AuthenticationIP* Web_Server::_head  = NULL;
    uint8_t           Web_Server::_nb_ip = 0;
    const int         MAX_AUTH_IP        = 10;
#    endif
    String                    Web_Server::_uploadFilename;
    const FileSystem::FsInfo* Web_Server::_uploadFs;
    FileStream*               Web_Server::_uploadFile = nullptr;

    EnumSetting* http_enable;
    IntSetting*  http_port;

    Web_Server::Web_Server() {
        http_port   = new IntSetting("HTTP Port", WEBSET, WA, "ESP121", "HTTP/Port", DEFAULT_HTTP_PORT, MIN_HTTP_PORT, MAX_HTTP_PORT, NULL);
        http_enable = new EnumSetting("HTTP Enable", WEBSET, WA, "ESP120", "HTTP/Enable", DEFAULT_HTTP_STATE, &onoffOptions, NULL);
    }
    Web_Server::~Web_Server() { end(); }

    long Web_Server::get_client_ID() { return _id_connection; }

    bool Web_Server::begin() {
        bool no_error = true;
        _setupdone    = false;

        if (!WebUI::http_enable->get()) {
            return false;
        }
        _port = WebUI::http_port->get();

        //create instance
        _webserver = new WebServer(_port);
#    ifdef ENABLE_AUTHENTICATION
        //here the list of headers to be recorded
        const char* headerkeys[]   = { "Cookie" };
        size_t      headerkeyssize = sizeof(headerkeys) / sizeof(char*);
        //ask server to track these headers
        _webserver->collectHeaders(headerkeys, headerkeyssize);
#    endif
        _socket_server = new WebSocketsServer(_port + 1);
        _socket_server->begin();
        _socket_server->onEvent(handle_Websocket_Event);

        //Websocket output
        serial2Socket.attachWS(_socket_server);
        allChannels.registration(&WebUI::serial2Socket);

        //events functions
        //_web_events->onConnect(handle_onevent_connect);
        //events management
        // _webserver->addHandler(_web_events);

        //Websocket function
        //_web_socket->onEvent(handle_Websocket_Event);
        //Websocket management
        //_webserver->addHandler(_web_socket);

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
        _webserver->on("/reload_blocked", HTTP_ANY, blockReload);
        _webserver->on("/feedhold_reload", HTTP_ANY, feedholdAndReload);

        //LocalFS
        _webserver->on("/files", HTTP_ANY, listLocalFiles, uploadLocalFile);

        //web update
        _webserver->on("/updatefw", HTTP_ANY, handleUpdate, WebUpdateUpload);

        //Direct SD management
        _webserver->on("/upload", HTTP_ANY, listSDFiles, uploadSDFile);

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

        //SSDP service presentation
        if (WiFi.getMode() == WIFI_STA) {
            _webserver->on("/description.xml", HTTP_GET, handle_SSDP);
            //Add specific for SSDP
            SSDP.setSchemaURL("description.xml");
            SSDP.setHTTPPort(_port);
            SSDP.setName(wifi_config.Hostname());
            SSDP.setURL("/");
            SSDP.setDeviceType("upnp:rootdevice");
            /*Any customization could be here
        SSDP.setModelName (ESP32_MODEL_NAME);
        SSDP.setModelURL (ESP32_MODEL_URL);
        SSDP.setModelNumber (ESP_MODEL_NUMBER);
        SSDP.setManufacturer (ESP_MANUFACTURER_NAME);
        SSDP.setManufacturerURL (ESP_MANUFACTURER_URL);
        */

            //Start SSDP
            log_info("SSDP Started");
            SSDP.begin();
        }

        log_info("HTTP started on port " << WebUI::http_port->get());
        //start webserver
        _webserver->begin();

        //add mDNS
        if (WiFi.getMode() == WIFI_STA) {
            MDNS.addService("http", "tcp", _port);
        }

        _setupdone = true;
        return no_error;
    }

    void Web_Server::end() {
        _setupdone = false;

        SSDP.end();

        //remove mDNS
        mdns_service_remove("_http", "_tcp");

        if (_socket_server) {
            allChannels.deregistration(&WebUI::serial2Socket);
            delete _socket_server;
            _socket_server = NULL;
        }

        if (_webserver) {
            delete _webserver;
            _webserver = NULL;
        }

#    ifdef ENABLE_AUTHENTICATION
        while (_head) {
            AuthenticationIP* current = _head;
            _head                     = _head->_next;
            delete current;
        }
        _nb_ip = 0;
#    endif
    }

    //Root of Webserver/////////////////////////////////////////////////////

    void Web_Server::handle_root() {
        // If you load or reload WebUI while a program is running, there is a high
        // risk of stalling the motion because serving the index.html.gz file from
        // the local FLASH filesystem takes away a lot of CPU cycles.  If we get
        // a request for index.html.gz when running, reject it to preserve the motion
        // integrity.
        // This can make it hard to debug ISR IRAM problems, because the easiest
        // way to trigger such problems is to refresh WebUI during motion.
        // If you need to do such debugging, comment out this check temporarily.
        if (sys.state == State::Cycle || sys.state == State::Jog || sys.state == State::Homing) {
            _webserver->send(200,
                             "text/html",
                             "<!DOCTYPE html><html><body>"
                             "<script>window.location.assign('/reload_blocked');</script>"
                             "</body></html>");
            return;
        }

        if (!(_webserver->hasArg("forcefallback") && _webserver->arg("forcefallback") == "yes")) {
            if (streamFile("/index.html")) {
                return;
            }
        }

        //Send the default content if forced to or index.html or index.html.gz could not be sent
        _webserver->sendHeader("Content-Encoding", "gzip");
        _webserver->send_P(200, "text/html", PAGE_NOFILES, PAGE_NOFILES_SIZE);
    }

    // Send a file, either the specified path or path.gz
    bool Web_Server::streamFile(String path, const FileSystem::FsInfo& fs) {
        FileStream* file;
        try {
            file = new FileStream(path, "r", fs);
        } catch (const Error err) {
            try {
                file = new FileStream(path + ".gz", "r", fs);
            } catch (const Error err) { return false; }
        }
        _webserver->streamFile(*file, getContentType(path));
        delete file;
        return true;
    }

    void Web_Server::sendWithOurAddress(String content) {
        auto   ip    = WiFi.getMode() == WIFI_STA ? WiFi.localIP() : WiFi.softAPIP();
        String ipstr = ip.toString();
        if (_port != 80) {
            ipstr += ":";
            ipstr += String(_port);
        }

        content.replace("$WEB_ADDRESS$", ipstr);
        content.replace("$QUERY$", _webserver->uri());
        _webserver->send(200, "text/html", content);
    }

    // Captive Portal Page for use in AP mode
    const char PAGE_CAPTIVE[] =
        "<HTML>\n<HEAD>\n<title>Captive Portal</title> \n</HEAD>\n<BODY>\n<CENTER>Captive Portal page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void Web_Server::sendCaptivePortal() { sendWithOurAddress(PAGE_CAPTIVE); }

    //Default 404 page that is sent when a request cannot be satisfied
    const char PAGE_404[] =
        "<HTML>\n<HEAD>\n<title>Redirecting...</title> \n</HEAD>\n<BODY>\n<CENTER>Unknown page : $QUERY$- you will be "
        "redirected...\n<BR><BR>\nif not redirected, <a href='http://$WEB_ADDRESS$'>click here</a>\n<BR><BR>\n<PROGRESS name='prg' "
        "id='prg'></PROGRESS>\n\n<script>\nvar i = 0; \nvar x = document.getElementById(\"prg\"); \nx.max=5; \nvar "
        "interval=setInterval(function(){\ni=i+1; \nvar x = document.getElementById(\"prg\"); \nx.value=i; \nif (i>5) "
        "\n{\nclearInterval(interval);\nwindow.location.href='/';\n}\n},1000);\n</script>\n</CENTER>\n</BODY>\n</HTML>\n\n";

    void Web_Server::send404Page() { sendWithOurAddress(PAGE_404); }

    // Handle filenames and other things that are not explicitly registered
    void Web_Server::handle_not_found() {
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _webserver->sendContent_P("HTTP/1.1 301 OK\r\nLocation: /\r\nCache-Control: no-cache\r\n\r\n");
            //_webserver->client().stop();
            return;
        }

        String path = _webserver->urlDecode(_webserver->uri());

        if (path.startsWith("/api/")) {
            _webserver->send(404);
            return;
        }

        if (streamFile(path)) {
            return;
        }

        if (WiFi.getMode() == WIFI_AP) {
            sendCaptivePortal();
            return;
        }

        // This lets the user customize the not-found page by
        // putting a "404.htm" file on the local filesystem
        if (streamFile("/404.htm")) {
            return;
        }

        send404Page();
    }

    //http SSDP xml presentation
    void Web_Server::handle_SSDP() {
        StreamString sschema;
        if (sschema.reserve(1024)) {
            _webserver->send(500);
        }
        String templ = "<?xml version=\"1.0\"?>"
                       "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
                       "<specVersion>"
                       "<major>1</major>"
                       "<minor>0</minor>"
                       "</specVersion>"
                       "<URLBase>http://%s:%u/</URLBase>"
                       "<device>"
                       "<deviceType>upnp:rootdevice</deviceType>"
                       "<friendlyName>%s</friendlyName>"
                       "<presentationURL>/</presentationURL>"
                       "<serialNumber>%s</serialNumber>"
                       "<modelName>ESP32</modelName>"
                       "<modelNumber>Marlin</modelNumber>"
                       "<modelURL>http://espressif.com/en/products/hardware/esp-wroom-32/overview</modelURL>"
                       "<manufacturer>Espressif Systems</manufacturer>"
                       "<manufacturerURL>http://espressif.com</manufacturerURL>"
                       "<UDN>uuid:%s</UDN>"
                       "</device>"
                       "</root>\r\n"
                       "\r\n";
        char     uuid[37];
        String   sip    = WiFi.localIP().toString();
        uint32_t chipId = (uint16_t)(ESP.getEfuseMac() >> 32);
        sprintf(uuid,
                "38323636-4558-4dda-9188-cda0e6%02x%02x%02x",
                (uint16_t)((chipId >> 16) & 0xff),
                (uint16_t)((chipId >> 8) & 0xff),
                (uint16_t)chipId & 0xff);
        String serialNumber = String(chipId);
        sschema.printf(templ.c_str(), sip.c_str(), _port, wifi_config.Hostname().c_str(), serialNumber.c_str(), uuid);
        _webserver->send(200, "text/xml", (String)sschema);
    }

    void Web_Server::_handle_web_command(bool silent) {
        //to save time if already disconnected
        //if (_webserver->hasArg ("PAGEID") ) {
        //    if (_webserver->arg ("PAGEID").length() > 0 ) {
        //       if (_webserver->arg ("PAGEID").toInt() != _id_connection) {
        //       _webserver->send (200, "text/plain", "Invalid command");
        //       return;
        //       }
        //    }
        //}
        AuthenticationLevel auth_level = is_authenticated();
        String              cmd        = "";
        if (_webserver->hasArg("plain")) {
            cmd = _webserver->arg("plain");
        } else if (_webserver->hasArg("commandText")) {
            cmd = _webserver->arg("commandText");
        } else {
            _webserver->send(200, "text/plain", "Invalid command");
            return;
        }

        //if it is internal command [ESPXXX]<parameter>
        cmd.trim();
        int ESPpos = cmd.indexOf("[ESP");
        if (ESPpos > -1) {
            char line[256];
            strncpy(line, cmd.c_str(), 255);
            webClient.attachWS(_webserver, silent);
            Error  err = settings_execute_line(line, webClient, auth_level);
            String answer;
            if (err == Error::Ok) {
                answer = "ok\n";
            } else {
                const char* msg = errorString(err);
                answer          = "Error: ";
                if (msg) {
                    answer += msg;
                } else {
                    answer += static_cast<int>(err);
                }
                answer += "\n";
            }
            if (!webClient.anyOutput()) {
                _webserver->send(err != Error::Ok ? 500 : 200, "text/plain", answer);
            }
            webClient.detachWS();
        } else {  //execute GCODE
            if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
                _webserver->send(401, "text/plain", "Authentication failed\n");
                return;
            }
            if (!silent) {
                // 0xC2 is an HTML encoding prefix that, in UTF-8 mode,
                // precedes 0x90 and 0xa0-0bf, which are GRBL realtime commands.
                // There are other encodings for 0x91-0x9f, so I am not sure
                // how - or whether - those commands work.
                // Ref: https://www.w3schools.com/tags/ref_urlencode.ASP
                String prefix(0xc2);
                cmd.replace(prefix, "");
            }
            bool hasError = false;
            if (cmd.length() == 1 && is_realtime_command(cmd[0])) {
                serial2Socket.pushRT(cmd[0]);
            } else {
                if (!cmd.endsWith("\n")) {
                    cmd += '\n';
                }
                hasError = !serial2Socket.push(cmd.c_str());
            }
            _webserver->send(200, "text/plain", hasError ? "Error" : "");
        }
    }

    //login status check
    void Web_Server::handle_login() {
#    ifdef ENABLE_AUTHENTICATION
        String smsg;
        String sUser, sPassword;
        String auths;
        int    code            = 200;
        bool   msg_alert_error = false;
        //disconnect can be done anytime no need to check credential
        if (_webserver->hasArg("DISCONNECT")) {
            String cookie = _webserver->header("Cookie");
            int    pos    = cookie.indexOf("ESPSESSIONID=");
            String sessionID;
            if (pos != -1) {
                int pos2  = cookie.indexOf(";", pos);
                sessionID = cookie.substring(pos + strlen("ESPSESSIONID="), pos2);
            }
            ClearAuthIP(_webserver->client().remoteIP(), sessionID.c_str());
            _webserver->sendHeader("Set-Cookie", "ESPSESSIONID=0");
            _webserver->sendHeader("Cache-Control", "no-cache");

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
                sUser = _webserver->arg("USER");
                if (!((sUser == DEFAULT_ADMIN_LOGIN) || (sUser == DEFAULT_USER_LOGIN))) {
                    msg_alert_error = true;
                    smsg            = "Error : Incorrect User";
                    code            = 401;
                }

                if (msg_alert_error == false) {
                    //Password
                    sPassword             = _webserver->arg("PASSWORD");
                    String sadminPassword = admin_password->get();
                    String suserPassword  = user_password->get();

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
                String newpassword = _webserver->arg("NEWPASSWORD");

                char pwdbuf[MAX_LOCAL_PASSWORD_LENGTH + 1];
                newpassword.toCharArray(pwdbuf, MAX_LOCAL_PASSWORD_LENGTH + 1);

                if (COMMANDS::isLocalPasswordValid(pwdbuf)) {
                    Error err;

                    if (sUser == DEFAULT_ADMIN_LOGIN) {
                        err = admin_password->setStringValue(pwdbuf);
                    } else {
                        err = user_password->setStringValue(pwdbuf);
                    }
                    if (err != Error::Ok) {
                        msg_alert_error = true;
                        smsg            = "Error: Cannot apply changes";
                        code            = 500;
                    }
                } else {
                    msg_alert_error = true;
                    smsg            = "Error: Incorrect password";
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
                        String tmps = "ESPSESSIONID=";
                        tmps += current_auth->sessionID;
                        _webserver->sendHeader("Set-Cookie", tmps);
                        _webserver->sendHeader("Cache-Control", "no-cache");
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

            sendAuth(smsg, auths, "");
        } else {
            if (auth_level != AuthenticationLevel::LEVEL_GUEST) {
                String cookie = _webserver->header("Cookie");
                int    pos    = cookie.indexOf("ESPSESSIONID=");
                String sessionID;
                if (pos != -1) {
                    int pos2                            = cookie.indexOf(";", pos);
                    sessionID                           = cookie.substring(pos + strlen("ESPSESSIONID="), pos2);
                    AuthenticationIP* current_auth_info = GetAuth(_webserver->client().remoteIP(), sessionID.c_str());
                    if (current_auth_info != NULL) {
                        sUser = current_auth_info->userID;
                    }
                }
            }
            sendAuth("Ok", auths, sUser);
        }
#    else
        sendAuth("Ok", "admin", "");
#    endif
    }

    // This page is used when you try to reload WebUI during motion,
    // to avoid interrupting that motion.  It lets you wait until
    // motion is finished or issue a feedhold.
    void Web_Server::blockReload() {
        _webserver->send(200,
                         "text/html",
                         "<!DOCTYPE html><html><body>"
                         "<h3>Cannot load WebUI while moving</h3>"
                         "<button onclick='window.location.replace(\"/\")'>Retry</button>"
                         "&nbsp;Retry (you must first wait for motion to finish)<br><br>"
                         "<button onclick='window.location.replace(\"/feedhold_reload\")'>Feedhold</button>"
                         "&nbsp;Stop the motion with feedhold and then retry<br>"
                         "</body></html>");
    }

    // This page issues a feedhold to pause the motion then retries the WebUI reload
    void Web_Server::feedholdAndReload() {
        // Send feedhold to FluidNC
        serial2Socket.pushRT('!');

        // Go to the main page
        _webserver->send(200,
                         "text/html",
                         "<!DOCTYPE html><html><body>"
                         "<script>window.location.replace('/');</script>"
                         "</body></html>");
    }

    void Web_Server::listLocalFiles() { listFiles(FileSystem::localfs); }
    void Web_Server::listSDFiles() { listFiles(FileSystem::sd); }

    void Web_Server::listFiles(const FileSystem::FsInfo& fs) {
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level == AuthenticationLevel::LEVEL_GUEST) {
            _uploadStatus = UploadStatus::NONE;
            sendAuthFailed();
            return;
        }

        String sstatus = "Ok";
        if (_uploadStatus == UploadStatus::FAILED) {
            sstatus = "Upload failed";
        }
        _uploadStatus = UploadStatus::NONE;

        //get current path
        String path = _webserver->hasArg("path") ? _webserver->arg("path") : "/";

        //check if query need some action
        if (_webserver->hasArg("action") && _webserver->hasArg("filename")) {
            String action   = _webserver->arg("action");
            String filename = _webserver->arg("filename");
            try {
                if (action == "delete") {
                    if (FileSystem(path, fs).deleteFile(filename)) {
                        sstatus = filename + " deleted";
                    } else {
                        sstatus = "Cannot delete ";
                        sstatus += filename;
                    }
                } else if (action == "deletedir") {
                    if (FileSystem(path, fs).deleteDir(filename)) {
                        sstatus = filename + " deleted";
                    } else {
                        sstatus = "Cannot delete ";
                        sstatus += filename;
                    }
                } else if (action == "createdir") {
                    if (!FileSystem(path, fs).mkdir(filename)) {
                        sstatus = "Cannot create ";
                        sstatus += filename;
                    } else {
                        sstatus = filename + " created";
                    }
                }
            } catch (const Error err) { sstatus = FSError(err); }
        }

        // Send a list of the files
        try {
            StreamString s;
            FileSystem(path, fs).listJSON(sstatus, s);
            sendJSON(200, s);
        } catch (const Error err) { sendStatus(200, FSError(err)); }
    }

    //push error code and message to websocket
    void Web_Server::pushError(int code, const char* st, bool web_error, uint16_t timeout) {
        if (_socket_server && st) {
            String s = "ERROR:" + String(code) + ":";
            s += st;
            _socket_server->sendTXT(_id_connection, s);
            if (web_error != 0 && _webserver && _webserver->client().available() > 0) {
                _webserver->send(web_error, "text/xml", st);
            }

            uint32_t start_time = millis();
            while ((millis() - start_time) < timeout) {
                _socket_server->loop();
                delay(10);
            }
        }
    }

    //abort reception of packages
    void Web_Server::cancelUpload() {
        if (_webserver && _webserver->client().available() > 0) {
            HTTPUpload& upload = _webserver->upload();
            upload.status      = UPLOAD_FILE_ABORTED;
            errno              = ECONNABORTED;
            _webserver->client().stop();
            delay(100);
        }
    }

    void Web_Server::uploadLocalFile() { uploadFile(FileSystem::localfs); }
    void Web_Server::uploadSDFile() { uploadFile(FileSystem::sd); }
    void Web_Server::uploadFile(const FileSystem::FsInfo& fs) {
        HTTPUpload& upload = _webserver->upload();
        //this is only for admin and user
        if (is_authenticated() == AuthenticationLevel::LEVEL_GUEST) {
            _uploadStatus = UploadStatus::FAILED;
            log_info("Upload rejected");
            sendAuthFailed();
            pushError(ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
            return;
        }
        if ((_uploadStatus != UploadStatus::FAILED) || (upload.status == UPLOAD_FILE_START)) {
            if (upload.status == UPLOAD_FILE_START) {
                String sizeargname = upload.filename + "S";
                size_t filesize    = _webserver->hasArg(sizeargname) ? _webserver->arg(sizeargname).toInt() : 0;
                uploadStart(upload.filename, filesize, fs);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                uploadWrite(upload.buf, upload.currentSize);
            } else if (upload.status == UPLOAD_FILE_END) {
                String sizeargname = upload.filename + "S";
                size_t filesize    = _webserver->hasArg(sizeargname) ? _webserver->arg(sizeargname).toInt() : 0;
                uploadEnd(filesize);
            } else {  //Upload cancelled
                uploadStop();
                return;
            }
        }
        uploadCheck();
    }

    void Web_Server::sendJSON(int code, const String& s) {
        _webserver->sendHeader("Cache-Control", "no-cache");
        _webserver->send(200, "application/json", s);
    }

    void Web_Server::sendAuth(const String& status, const String& level, const String& user) {
        StreamString s;
        JSONencoder  j(false, s);
        j.begin();
        j.member("status", status);
        if (level != "") {
            j.member("authentication_lvl", level);
        }
        if (user != "") {
            j.member("user", user);
        }
        j.end();
        sendJSON(200, s);
    }

    void Web_Server::sendStatus(int code, const String& status) {
        StreamString s;
        JSONencoder  j(false, s);
        j.begin();
        j.member("status", status);
        j.end();
        sendJSON(code, s);
    }

    void Web_Server::sendAuthFailed() { sendStatus(401, "Authentication failed"); }

    //Web Update handler
    void Web_Server::handleUpdate() {
        AuthenticationLevel auth_level = is_authenticated();
        if (auth_level != AuthenticationLevel::LEVEL_ADMIN) {
            _uploadStatus = UploadStatus::NONE;
            _webserver->send(403, "text/plain", "Not allowed, log in first!\n");
            return;
        }

        sendStatus(200, String(int(_uploadStatus)));

        //if success restart
        if (_uploadStatus == UploadStatus::SUCCESSFUL) {
            delay_ms(1000);
            COMMANDS::restart_MCU();
        } else {
            _uploadStatus = UploadStatus::NONE;
        }
    }

    //File upload for Web update
    void Web_Server::WebUpdateUpload() {
        static size_t   last_upload_update;
        static uint32_t maxSketchSpace = 0;

        //only admin can update FW
        if (is_authenticated() != AuthenticationLevel::LEVEL_ADMIN) {
            _uploadStatus = UploadStatus::FAILED;
            log_info("Upload rejected");
            pushError(ESP_ERROR_AUTHENTICATION, "Upload rejected", 401);
        } else {
            //get current file ID
            HTTPUpload& upload = _webserver->upload();
            if ((_uploadStatus != UploadStatus::FAILED) || (upload.status == UPLOAD_FILE_START)) {
                //Upload start
                //**************
                if (upload.status == UPLOAD_FILE_START) {
                    log_info("Update Firmware");
                    _uploadStatus      = UploadStatus::ONGOING;
                    String sizeargname = upload.filename + "S";
                    if (_webserver->hasArg(sizeargname)) {
                        maxSketchSpace = _webserver->arg(sizeargname).toInt();
                    }
                    //check space
                    size_t flashsize = 0;
                    if (esp_ota_get_running_partition()) {
                        const esp_partition_t* partition = esp_ota_get_next_update_partition(NULL);
                        if (partition) {
                            flashsize = partition->size;
                        }
                    }
                    if (flashsize < maxSketchSpace) {
                        pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                        _uploadStatus = UploadStatus::FAILED;
                        log_info("Update cancelled");
                    }
                    if (_uploadStatus != UploadStatus::FAILED) {
                        last_upload_update = 0;
                        if (!Update.begin()) {  //start with max available size
                            _uploadStatus = UploadStatus::FAILED;
                            log_info("Update cancelled");
                            pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                        } else {
                            log_info("Update 0%");
                        }
                    }
                    //Upload write
                    //**************
                } else if (upload.status == UPLOAD_FILE_WRITE) {
                    vTaskDelay(1 / portTICK_RATE_MS);
                    //check if no error
                    if (_uploadStatus == UploadStatus::ONGOING) {
                        if (((100 * upload.totalSize) / maxSketchSpace) != last_upload_update) {
                            if (maxSketchSpace > 0) {
                                last_upload_update = (100 * upload.totalSize) / maxSketchSpace;
                            } else {
                                last_upload_update = upload.totalSize;
                            }

                            log_info("Update " << last_upload_update << "%");
                        }
                        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                            _uploadStatus = UploadStatus::FAILED;
                            log_info("Update write failed");
                            pushError(ESP_ERROR_FILE_WRITE, "File write failed");
                        }
                    }
                    //Upload end
                    //**************
                } else if (upload.status == UPLOAD_FILE_END) {
                    if (Update.end(true)) {  //true to set the size to the current progress
                        //Now Reboot
                        log_info("Update 100%");
                        _uploadStatus = UploadStatus::SUCCESSFUL;
                    } else {
                        _uploadStatus = UploadStatus::FAILED;
                        log_info("Update failed");
                        pushError(ESP_ERROR_UPLOAD, "Update upload failed");
                    }
                } else if (upload.status == UPLOAD_FILE_ABORTED) {
                    log_info("Update failed");
                    _uploadStatus = UploadStatus::FAILED;
                    return;
                }
            }
        }

        if (_uploadStatus == UploadStatus::FAILED) {
            cancelUpload();
            Update.end();
        }
    }

    const char* Web_Server::FSError(Error err) {
        switch (err) {
            case Error::FsFailedBusy:
                return "Filesystem Busy";
            case Error::FsFailedMount:
                return "Filesystem Unavailable";
            default:
                return "Filesystem Error";
        }
    }

    void Web_Server::uploadStart(const String& filename, size_t filesize, const FileSystem::FsInfo& fs) {
        _uploadFs       = &fs;
        _uploadFilename = filename;
        try {
            FileSystem fso(filename, fs);
            fso.deleteFile();

            if (filesize > (fso.totalBytes() - fso.usedBytes())) {
                _uploadStatus = UploadStatus::FAILED;
                log_info("Upload not enough space");
                pushError(ESP_ERROR_NOT_ENOUGH_SPACE, "Upload rejected, not enough space");
                return;
            }
        } catch (const Error err) {
            pushError(ESP_ERROR_UPLOAD, "Upload rejected - filesystem unavailable");
            return;
        }
        _uploadStatus = UploadStatus::ONGOING;

        //Create file for writing
        try {
            _uploadFile   = new FileStream(_uploadFilename, "w", fs);
            _uploadStatus = UploadStatus::ONGOING;
        } catch (const Error err) {
            _uploadFile   = nullptr;
            _uploadStatus = UploadStatus::FAILED;
            log_info("Upload failed - cannot create file");
            pushError(ESP_ERROR_FILE_CREATION, "File creation failed");
        }
    }

    void Web_Server::uploadWrite(uint8_t* buffer, size_t length) {
        vTaskDelay(1 / portTICK_RATE_MS);
        if (_uploadFile && _uploadStatus == UploadStatus::ONGOING) {
            //no error write post data
            if (length != _uploadFile->write(buffer, length)) {
                _uploadStatus = UploadStatus::FAILED;
                log_info("Upload failed - file write failed");
                pushError(ESP_ERROR_FILE_WRITE, "File write failed");
            }
        } else {  //if error set flag UploadStatus::FAILED
            _uploadStatus = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(ESP_ERROR_FILE_WRITE, "File not open");
        }
    }

    void Web_Server::uploadEnd(size_t filesize) {
        //if file is open close it
        if (_uploadFile) {
            delete _uploadFile;
            _uploadFile = nullptr;

            // Check size
            if (filesize) {
                uint32_t actual_size;
                try {
                    FileStream* theFile = new FileStream(_uploadFilename, "r", *_uploadFs);
                    actual_size         = theFile->size();
                    delete theFile;
                } catch (const Error err) { actual_size = 0; }

                if (filesize != actual_size) {
                    _uploadStatus = UploadStatus::FAILED;
                    pushError(ESP_ERROR_UPLOAD, "File upload mismatch");
                    log_info("Upload failed - size mismatch - exp " << filesize << " got " << actual_size);
                }
            }
        } else {
            _uploadStatus = UploadStatus::FAILED;
            log_info("Upload failed - file not open");
            pushError(ESP_ERROR_FILE_CLOSE, "File close failed");
        }
        if (_uploadStatus == UploadStatus::ONGOING) {
            _uploadStatus = UploadStatus::SUCCESSFUL;
        } else {
            _uploadStatus = UploadStatus::FAILED;
            pushError(ESP_ERROR_UPLOAD, "Upload error 8");
        }
    }
    void Web_Server::uploadStop() {
        _uploadStatus = UploadStatus::FAILED;
        log_info("Upload cancelled");
        if (_uploadFile) {
            delete _uploadFile;
            _uploadFile = nullptr;
        }
    }
    void Web_Server::uploadCheck() {
        if (_uploadStatus == UploadStatus::FAILED) {
            cancelUpload();
            if (_uploadFile) {
                delete _uploadFile;
                _uploadFile = nullptr;
            }
            FileSystem(_uploadFilename, *_uploadFs).deleteFile();
        }
    }

    void Web_Server::handle() {
        static uint32_t start_time = millis();
        if (WiFi.getMode() == WIFI_AP) {
            dnsServer.processNextRequest();
        }
        if (_webserver) {
            _webserver->handleClient();
        }
        if (_socket_server && _setupdone) {
            _socket_server->loop();
        }
        if ((millis() - start_time) > 10000 && _socket_server) {
            String s = "PING:";
            s += String(_id_connection);
            _socket_server->broadcastTXT(s);
            start_time = millis();
        }
    }

    void Web_Server::handle_Websocket_Event(uint8_t num, uint8_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                break;
            case WStype_CONNECTED: {
                IPAddress ip = _socket_server->remoteIP(num);
                String    s  = "CURRENT_ID:" + String(num);
                // send message to client
                _id_connection = num;
                _socket_server->sendTXT(_id_connection, s);
                s = "ACTIVE_ID:" + String(_id_connection);
                _socket_server->broadcastTXT(s);
            } break;
            case WStype_TEXT:
            case WStype_BIN:
                serial2Socket.push(payload, length);
                break;
            default:
                break;
        }
    }

    //helper to extract content type from file extension
    //Check what is the content tye according extension file
    String Web_Server::getContentType(String filename) {
        String file_name = filename;
        file_name.toLowerCase();
        if (filename.endsWith(".htm")) {
            return "text/html";
        } else if (file_name.endsWith(".html")) {
            return "text/html";
        } else if (file_name.endsWith(".css")) {
            return "text/css";
        } else if (file_name.endsWith(".js")) {
            return "application/javascript";
        } else if (file_name.endsWith(".png")) {
            return "image/png";
        } else if (file_name.endsWith(".gif")) {
            return "image/gif";
        } else if (file_name.endsWith(".jpeg")) {
            return "image/jpeg";
        } else if (file_name.endsWith(".jpg")) {
            return "image/jpeg";
        } else if (file_name.endsWith(".ico")) {
            return "image/x-icon";
        } else if (file_name.endsWith(".xml")) {
            return "text/xml";
        } else if (file_name.endsWith(".pdf")) {
            return "application/x-pdf";
        } else if (file_name.endsWith(".zip")) {
            return "application/x-zip";
        } else if (file_name.endsWith(".gz")) {
            return "application/x-gzip";
        } else if (file_name.endsWith(".txt")) {
            return "text/plain";
        }
        return "application/octet-stream";
    }

    //check authentification
    AuthenticationLevel Web_Server::is_authenticated() {
#    ifdef ENABLE_AUTHENTICATION
        if (_webserver->hasHeader("Cookie")) {
            String cookie = _webserver->header("Cookie");
            int    pos    = cookie.indexOf("ESPSESSIONID=");
            if (pos != -1) {
                int       pos2      = cookie.indexOf(";", pos);
                String    sessionID = cookie.substring(pos + strlen("ESPSESSIONID="), pos2);
                IPAddress ip        = _webserver->client().remoteIP();
                //check if cookie can be reset and clean table in same time
                return ResetAuthIP(ip, sessionID.c_str());
            }
        }
        return AuthenticationLevel::LEVEL_GUEST;
#    else
        return AuthenticationLevel::LEVEL_ADMIN;
#    endif
    }

#    ifdef ENABLE_AUTHENTICATION

    //add the information in the linked list if possible
    bool Web_Server::AddAuthIP(AuthenticationIP* item) {
        if (_nb_ip > MAX_AUTH_IP) {
            return false;
        }
        item->_next = _head;
        _head       = item;
        _nb_ip++;
        return true;
    }

    //Session ID based on IP and time using 16 char
    char* Web_Server::create_session_ID() {
        static char sessionID[17];
        //reset SESSIONID
        for (int i = 0; i < 17; i++) {
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

    bool Web_Server::ClearAuthIP(IPAddress ip, const char* sessionID) {
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
    AuthenticationIP* Web_Server::GetAuth(IPAddress ip, const char* sessionID) {
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
    AuthenticationLevel Web_Server::ResetAuthIP(IPAddress ip, const char* sessionID) {
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
#    endif
}
#endif
