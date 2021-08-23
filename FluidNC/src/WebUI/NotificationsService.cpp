// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

//Inspired by following sources
//* Line :
// - https://github.com/TridentTD/TridentTD_LineNotify
// - https://notify-bot.line.me/doc/en/
//* Pushover:
// - https://github.com/ArduinoHannover/Pushover
// - https://pushover.net/api
//* Email:
// - https://github.com/CosmicBoris/ESP8266SMTP
// - https://www.electronicshub.org/send-an-email-using-esp8266/

#include "NotificationsService.h"

namespace WebUI {
    NotificationsService notificationsservice;
}

#ifdef ENABLE_WIFI

#    include "WifiConfig.h"   // ESP_PUSHOVER_NOTIFICATION
#    include "WebSettings.h"  // notification_ts
#    include "Commands.h"
#    include "../Machine/MachineConfig.h"

#    include <WiFiClientSecure.h>
#    include <base64.h>

namespace WebUI {
    static const int   PUSHOVERTIMEOUT = 5000;
    static const char* PUSHOVERSERVER  = "api.pushover.net";
    static const int   PUSHOVERPORT    = 443;

    static const int   LINETIMEOUT = 5000;
    static const char* LINESERVER  = "notify-api.line.me";
    static const int   LINEPORT    = 443;

    static const int EMAILTIMEOUT = 5000;

    NotificationsService::NotificationsService() {
        _started          = false;
        _notificationType = 0;
        _token1           = "";
        _token1           = "";
        _settings         = "";
    }

    bool Wait4Answer(WiFiClientSecure& client, const char* linetrigger, const char* expected_answer, uint32_t timeout) {
        if (client.connected()) {
            String   answer;
            uint32_t start_time = millis();
            while (client.connected() && ((millis() - start_time) < timeout)) {
                answer = client.readStringUntil('\n');
                log_d("Answer: %s", answer.c_str());
                if ((answer.indexOf(linetrigger) != -1) || (strlen(linetrigger) == 0)) {
                    break;
                }
                COMMANDS::wait(10);
            }
            if (strlen(expected_answer) == 0) {
                log_d("Answer ignored as requested");
                return true;
            }
            if (answer.indexOf(expected_answer) == -1) {
                log_d("Did not got answer!");
                return false;
            } else {
                log_d("Got expected answer");
                return true;
            }
        }
        log_d("Failed to send message");
        return false;
    }

    bool NotificationsService::started() { return _started; }

    const char* NotificationsService::getTypeString() {
        switch (_notificationType) {
            case ESP_PUSHOVER_NOTIFICATION:
                return "Pushover";
            case ESP_EMAIL_NOTIFICATION:
                return "Email";
            case ESP_LINE_NOTIFICATION:
                return "Line";
            default:
                return "None";
        }
    }

    bool NotificationsService::sendMSG(const char* title, const char* message) {
        if (!_started) {
            return false;
        }
        if (!((strlen(title) == 0) && (strlen(message) == 0))) {
            switch (_notificationType) {
                case ESP_PUSHOVER_NOTIFICATION:
                    return sendPushoverMSG(title, message);
                    break;
                case ESP_EMAIL_NOTIFICATION:
                    return sendEmailMSG(title, message);
                    break;
                case ESP_LINE_NOTIFICATION:
                    return sendLineMSG(title, message);
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    //Messages are currently limited to 1024 4-byte UTF-8 characters
    //but we do not do any check
    bool NotificationsService::sendPushoverMSG(const char* title, const char* message) {
        String           data;
        String           postcmd;
        bool             res;
        WiFiClientSecure Notificationclient;
        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
            log_d("Error connecting  server %s:%d", _serveraddress.c_str(), _port);
            return false;
        }
        //build data for post
        data = "user=";
        data += _token1;
        data += "&token=";
        data += _token2;
        ;
        data += "&title=";
        data += title;
        data += "&message=";
        data += message;
        data += "&device=";
        data += wifi_config.Hostname();
        //build post query
        postcmd = "POST /1/messages.json HTTP/1.1\r\nHost: api.pushover.net\r\nConnection: close\r\nCache-Control: no-cache\r\nUser-Agent: "
                  "ESP3D\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nContent-Length: ";
        postcmd += data.length();
        postcmd += "\r\n\r\n";
        postcmd += data;
        log_d("Query: %s", postcmd.c_str());
        //send query
        Notificationclient.print(postcmd);
        res = Wait4Answer(Notificationclient, "{", "\"status\":1", PUSHOVERTIMEOUT);
        Notificationclient.stop();
        return res;
    }
    bool NotificationsService::sendEmailMSG(const char* title, const char* message) {
        WiFiClientSecure Notificationclient;
        log_d("Connect to server");
        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
            log_d("Error connecting  server %s:%d", _serveraddress.c_str(), _port);
            return false;
        }
        //Check answer of connection
        if (!Wait4Answer(Notificationclient, "220", "220", EMAILTIMEOUT)) {
            log_d("Connection failed!");
            return false;
        }
        //Do HELO
        log_d("HELO");
        Notificationclient.print("HELO friend\r\n");
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            log_d("HELO failed!");
            return false;
        }
        log_d("AUTH LOGIN");
        //Request AUthentication
        Notificationclient.print("AUTH LOGIN\r\n");
        if (!Wait4Answer(Notificationclient, "334", "334", EMAILTIMEOUT)) {
            log_d("AUTH LOGIN failed!");
            return false;
        }
        log_d("Send LOGIN");
        //sent Login
        Notificationclient.printf("%s\r\n", _token1.c_str());
        if (!Wait4Answer(Notificationclient, "334", "334", EMAILTIMEOUT)) {
            log_d("Sent login failed!");
            return false;
        }
        log_d("Send PASSWORD");
        //Send password
        Notificationclient.printf("%s\r\n", _token2.c_str());
        if (!Wait4Answer(Notificationclient, "235", "235", EMAILTIMEOUT)) {
            log_d("Sent password failed!");
            return false;
        }
        log_d("MAIL FROM");
        //Send From
        Notificationclient.printf("MAIL FROM: <%s>\r\n", _settings.c_str());
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            log_d("MAIL FROM failed!");
            return false;
        }
        log_d("RCPT TO");
        //Send To
        Notificationclient.printf("RCPT TO: <%s>\r\n", _settings.c_str());
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            log_d("RCPT TO failed!");
            return false;
        }
        log_d("DATA");
        //Send Data
        Notificationclient.print("DATA\r\n");
        if (!Wait4Answer(Notificationclient, "354", "354", EMAILTIMEOUT)) {
            log_d("Preparing DATA failed!");
            return false;
        }
        log_d("Send message");
        //Send message
        Notificationclient.printf("From:ESP3D<%s>\r\n", _settings.c_str());
        Notificationclient.printf("To: <%s>\r\n", _settings.c_str());
        Notificationclient.printf("Subject: %s\r\n\r\n", title);
        Notificationclient.println(message);
        log_d("Send final dot");
        //Send Final dot
        Notificationclient.print(".\r\n");
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            log_d("Sending final dot failed!");
            return false;
        }
        log_d("QUIT");
        //Quit
        Notificationclient.print("QUIT\r\n");
        if (!Wait4Answer(Notificationclient, "221", "221", EMAILTIMEOUT)) {
            log_d("QUIT failed!");
            return false;
        }
        Notificationclient.stop();
        return true;
    }
    bool NotificationsService::sendLineMSG(const char* title, const char* message) {
        String           data;
        String           postcmd;
        bool             res;
        WiFiClientSecure Notificationclient;
        (void)title;
        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
            log_d("Error connecting  server %s:%d", _serveraddress.c_str(), _port);
            return false;
        }
        //build data for post
        data = "message=";
        data += message;
        //build post query
        postcmd = "POST /api/notify HTTP/1.1\r\nHost: notify-api.line.me\r\nConnection: close\r\nCache-Control: no-cache\r\nUser-Agent: "
                  "ESP3D\r\nAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nContent-Type: "
                  "application/x-www-form-urlencoded\r\n";
        postcmd += "Authorization: Bearer ";
        postcmd += _token1 + "\r\n";
        postcmd += "Content-Length: ";
        postcmd += data.length();
        postcmd += "\r\n\r\n";
        postcmd += data;
        log_d("Query: %s", postcmd.c_str());
        //send query
        Notificationclient.print(postcmd);
        res = Wait4Answer(Notificationclient, "{", "\"status\":200", LINETIMEOUT);
        Notificationclient.stop();
        return res;
    }
    //Email#serveraddress:port
    bool NotificationsService::getPortFromSettings() {
        String tmp = notification_ts->get();
        int    pos = tmp.lastIndexOf(':');
        if (pos == -1) {
            return false;
        }

        _port = tmp.substring(pos + 1).toInt();
        log_d("port : %d", _port);
        return _port > 0;
    }
    //Email#serveraddress:port
    bool NotificationsService::getServerAddressFromSettings() {
        String tmp  = notification_ts->get();
        int    pos1 = tmp.indexOf('#');
        int    pos2 = tmp.lastIndexOf(':');
        if ((pos1 == -1) || (pos2 == -1)) {
            return false;
        }

        //TODO add a check for valid email ?
        _serveraddress = tmp.substring(pos1 + 1, pos2);
        log_d("server : %s", _serveraddress.c_str());
        return true;
    }
    //Email#serveraddress:port
    bool NotificationsService::getEmailFromSettings() {
        String tmp = notification_ts->get();
        int    pos = tmp.indexOf('#');
        if (pos == -1) {
            return false;
        }
        _settings = tmp.substring(0, pos);
        log_d("email : %s", _settings.c_str());
        //TODO add a check for valid email ?
        return true;
    }

    bool NotificationsService::begin() {
        end();
        _notificationType = notification_type->get();
        switch (_notificationType) {
            case 0:  //no notification = no error but no start
                return true;
            case ESP_PUSHOVER_NOTIFICATION:
                _token1        = notification_t1->get();
                _token2        = notification_t2->get();
                _port          = PUSHOVERPORT;
                _serveraddress = PUSHOVERSERVER;
                break;
            case ESP_LINE_NOTIFICATION:
                _token1        = notification_t1->get();
                _port          = LINEPORT;
                _serveraddress = LINESERVER;
                break;
            case ESP_EMAIL_NOTIFICATION:
                _token1 = base64::encode(notification_t1->get());
                _token2 = base64::encode(notification_t2->get());
                if (!getEmailFromSettings() || !getPortFromSettings() || !getServerAddressFromSettings()) {
                    return false;
                }
                break;
            default:
                return false;
                break;
        }
        bool res = true;
        if (WiFi.getMode() != WIFI_STA) {
            res = false;
        }
        if (!res) {
            end();
        }
        _started = res;
        return _started;
    }

    void NotificationsService::end() {
        if (!_started) {
            return;
        }

        _started          = false;
        _notificationType = 0;
        _token1           = "";
        _token1           = "";
        _settings         = "";
        _serveraddress    = "";
        _port             = 0;
    }

    void NotificationsService::handle() {
        if (_started) {}
    }

    NotificationsService::~NotificationsService() { end(); }
}
#endif
