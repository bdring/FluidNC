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
    NotificationsService notificationsService __attribute__((init_priority(106)));
}

#ifdef ENABLE_WIFI

#    include "WebSettings.h"  // notification_ts
#    include "Commands.h"
#    include "WifiConfig.h"  // wifi_config.Hostname()
#    include "../Machine/MachineConfig.h"

#    include <WiFiClientSecure.h>
#    include <base64.h>

namespace WebUI {
    static const int PUSHOVER_NOTIFICATION = 1;
    static const int EMAIL_NOTIFICATION    = 2;
    static const int LINE_NOTIFICATION     = 3;

    static const int DEFAULT_NOTIFICATION_TYPE       = 0;
    static const int MIN_NOTIFICATION_TOKEN_LENGTH   = 0;
    static const int MAX_NOTIFICATION_TOKEN_LENGTH   = 63;
    static const int MAX_NOTIFICATION_SETTING_LENGTH = 127;

    static const char* DEFAULT_TOKEN = "";

    static const int   PUSHOVERTIMEOUT = 5000;
    static const char* PUSHOVERSERVER  = "api.pushover.net";
    static const int   PUSHOVERPORT    = 443;

    static const int   LINETIMEOUT = 5000;
    static const char* LINESERVER  = "notify-api.line.me";
    static const int   LINEPORT    = 443;

    static const int EMAILTIMEOUT = 5000;

    const enum_opt_t notificationOptions = {
        { "NONE", 0 },
        { "LINE", 3 },
        { "PUSHOVER", 1 },
        { "EMAIL", 2 },
    };
    EnumSetting*   notification_type;
    StringSetting* notification_t1;
    StringSetting* notification_t2;
    StringSetting* notification_ts;

    static Error showSetNotification(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP610
        if (*parameter == '\0') {
            log_stream(out, notification_type->getStringValue() << " " << notification_ts->getStringValue());
            return Error::Ok;
        }
        std::string s;

        if (!get_param(parameter, "type=", s)) {
            return Error::InvalidValue;
        }
        Error err;
        err = notification_type->setStringValue(s);
        if (err != Error::Ok) {
            return err;
        }

        if (!get_param(parameter, "T1=", s)) {
            return Error::InvalidValue;
        }
        err = notification_t1->setStringValue(s);
        if (err != Error::Ok) {
            return err;
        }

        if (!get_param(parameter, "T2=", s)) {
            return Error::InvalidValue;
        }
        err = notification_t2->setStringValue(s);
        if (err != Error::Ok) {
            return err;
        }

        if (!get_param(parameter, "TS=", s)) {
            return Error::InvalidValue;
        }
        err = notification_ts->setStringValue(s);
        if (err != Error::Ok) {
            return err;
        }
        return Error::Ok;
    }

    static Error sendMessage(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP600
        if (*parameter == '\0') {
            log_string(out, "Invalid message!");
            return Error::InvalidValue;
        }
        if (!notificationsService.sendMSG("GRBL Notification", parameter)) {
            log_string(out, "Cannot send message!");
            return Error::MessageFailed;
        }
        return Error::Ok;
    }

    NotificationsService::NotificationsService() {
        _started          = false;
        _notificationType = 0;
        _token1           = "";
        _token1           = "";
        _settings         = "";

        new WebCommand(
            "TYPE=NONE|PUSHOVER|EMAIL|LINE T1=token1 T2=token2 TS=settings", WEBCMD, WA, "ESP610", "Notification/Setup", showSetNotification);
        notification_ts = new StringSetting(
            "Notification Settings", WEBSET, WA, NULL, "Notification/TS", DEFAULT_TOKEN, 0, MAX_NOTIFICATION_SETTING_LENGTH);
        notification_t2 = new StringSetting("Notification Token 2",
                                            WEBSET,
                                            WA,
                                            NULL,
                                            "Notification/T2",
                                            DEFAULT_TOKEN,
                                            MIN_NOTIFICATION_TOKEN_LENGTH,
                                            MAX_NOTIFICATION_TOKEN_LENGTH);
        notification_t1 = new StringSetting("Notification Token 1",
                                            WEBSET,
                                            WA,
                                            NULL,
                                            "Notification/T1",
                                            DEFAULT_TOKEN,
                                            MIN_NOTIFICATION_TOKEN_LENGTH,
                                            MAX_NOTIFICATION_TOKEN_LENGTH);
        notification_type =
            new EnumSetting("Notification type", WEBSET, WA, NULL, "Notification/Type", DEFAULT_NOTIFICATION_TYPE, &notificationOptions);
        new WebCommand("message", WEBCMD, WU, "ESP600", "Notification/Send", sendMessage);
    }

    bool Wait4Answer(WiFiClientSecure& client, const char* linetrigger, const char* expected_answer, uint32_t timeout) {
        if (client.connected()) {
            std::string answer;
            uint32_t    start_time = millis();
            while (client.connected() && ((millis() - start_time) < timeout)) {
                answer = std::string(client.readStringUntil('\n').c_str());
                if ((answer.find(linetrigger) != std::string::npos) || (strlen(linetrigger) == 0)) {
                    break;
                }
                delay_ms(10);
                log_verbose("Received: '" << answer << "' (waiting 4 '" << expected_answer << "')");
            }
            if (strlen(expected_answer) == 0) {
                return true;
            }

            bool result = answer.find(expected_answer) != std::string::npos;
            if (!result) {
                if (answer.length()) {
                    log_debug("Received: '" << answer << "' (expected: '" << expected_answer << "')");
                } else {
                    log_debug("No answer (expected: " << expected_answer << ")");
                }
            }
            return result;
        }
        return false;
    }

    bool NotificationsService::started() { return _started; }

    const char* NotificationsService::getTypeString() {
        switch (_notificationType) {
            case PUSHOVER_NOTIFICATION:
                return "Pushover";
            case EMAIL_NOTIFICATION:
                return "Email";
            case LINE_NOTIFICATION:
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
                case PUSHOVER_NOTIFICATION:
                    return sendPushoverMSG(title, message);
                    break;
                case EMAIL_NOTIFICATION:
                    return sendEmailMSG(title, message);
                    break;
                case LINE_NOTIFICATION:
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
        std::string      data, postcmd;
        bool             res;
        WiFiClientSecure Notificationclient;
        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
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
        //send query
        Notificationclient.print(postcmd.c_str());
        res = Wait4Answer(Notificationclient, "{", "\"status\":1", PUSHOVERTIMEOUT);
        Notificationclient.stop();
        return res;
    }

    bool NotificationsService::sendEmailMSG(const char* title, const char* message) {
        WiFiClientSecure Notificationclient;
        // Switch off secure mode because the connect command always fails in secure mode:(
        Notificationclient.setInsecure();

        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
            //Read & log error message (in debug mode)
            if (atMsgLevel(MsgLevelDebug)) {
                char      errMsg[150];
                const int lastError = Notificationclient.lastError(errMsg, sizeof(errMsg));
                if (0 == lastError) {
                    errMsg[0] = 0;
                }
                log_debug("Cannot connect to " << _serveraddress.c_str() << ":" << _port << ", err: " << lastError << " - " << errMsg);
            }
            return false;
        }
        log_verbose("Connected to " << _serveraddress.c_str() << ":" << _port);

        //Check answer of connection
        if (!Wait4Answer(Notificationclient, "220", "220", EMAILTIMEOUT)) {
            return false;
        }
        //Do HELO
        Notificationclient.print("HELO friend\r\n");
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            return false;
        }
        //Request AUthentication
        Notificationclient.print("AUTH LOGIN\r\n");
        if (!Wait4Answer(Notificationclient, "334", "334", EMAILTIMEOUT)) {
            return false;
        }
        //sent Login
        Notificationclient.printf("%s\r\n", _token1.c_str());
        if (!Wait4Answer(Notificationclient, "334", "334", EMAILTIMEOUT)) {
            return false;
        }
        //Send password
        Notificationclient.printf("%s\r\n", _token2.c_str());
        if (!Wait4Answer(Notificationclient, "235", "235", EMAILTIMEOUT)) {
            return false;
        }
        //Send From
        Notificationclient.printf("MAIL FROM: <%s>\r\n", _settings.c_str());
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            return false;
        }
        //Send To
        Notificationclient.printf("RCPT TO: <%s>\r\n", _settings.c_str());
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            return false;
        }
        //Send Data
        Notificationclient.print("DATA\r\n");
        if (!Wait4Answer(Notificationclient, "354", "354", EMAILTIMEOUT)) {
            return false;
        }
        //Send message
        Notificationclient.printf("From:ESP3D<%s>\r\n", _settings.c_str());
        Notificationclient.printf("To: <%s>\r\n", _settings.c_str());
        Notificationclient.printf("Subject: %s\r\n\r\n", title);
        Notificationclient.println(message);
        //Send Final dot
        Notificationclient.print(".\r\n");
        if (!Wait4Answer(Notificationclient, "250", "250", EMAILTIMEOUT)) {
            return false;
        }
        //Quit
        Notificationclient.print("QUIT\r\n");
        if (!Wait4Answer(Notificationclient, "221", "221", EMAILTIMEOUT)) {
            return false;
        }
        Notificationclient.stop();
        return true;
    }
    bool NotificationsService::sendLineMSG(const char* title, const char* message) {
        std::string      data, postcmd;
        bool             res;
        WiFiClientSecure Notificationclient;
        (void)title;
        if (!Notificationclient.connect(_serveraddress.c_str(), _port)) {
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
        postcmd += std::to_string(data.length());
        postcmd += "\r\n\r\n";
        postcmd += data;
        //send query
        Notificationclient.print(postcmd.c_str());
        res = Wait4Answer(Notificationclient, "{", "\"status\":200", LINETIMEOUT);
        Notificationclient.stop();
        return res;
    }
    //Email#serveraddress:port
    bool NotificationsService::getPortFromSettings() {
        std::string tmp(notification_ts->get());
        size_t      pos = tmp.rfind(':');
        if (pos == std::string::npos) {
            return false;
        }

        try {
            _port = stoi(tmp.substr(pos + 1));
        } catch (...) { return false; }
        return true;
    }
    //Email#serveraddress:port
    bool NotificationsService::getServerAddressFromSettings() {
        std::string tmp(notification_ts->get());
        int         pos1 = tmp.find('#');
        int         pos2 = tmp.rfind(':');
        if ((pos1 == std::string::npos) || (pos2 == std::string::npos)) {
            return false;
        }

        //TODO add a check for valid email ?
        _serveraddress = tmp.substr(pos1 + 1, pos2 - pos1 - 1);
        return true;
    }
    //Email#serveraddress:port
    bool NotificationsService::getEmailFromSettings() {
        std::string tmp(notification_ts->get());
        int         pos = tmp.find('#');
        if (pos == std::string::npos) {
            return false;
        }
        _settings = tmp.substr(0, pos);
        //TODO add a check for valid email ?
        return true;
    }

    bool NotificationsService::begin() {
        end();
        _notificationType = notification_type->get();
        switch (_notificationType) {
            case 0:  //no notification = no error but no start
                return true;
            case PUSHOVER_NOTIFICATION:
                _token1        = notification_t1->get();
                _token2        = notification_t2->get();
                _port          = PUSHOVERPORT;
                _serveraddress = PUSHOVERSERVER;
                break;
            case LINE_NOTIFICATION:
                _token1        = notification_t1->get();
                _port          = LINEPORT;
                _serveraddress = LINESERVER;
                break;
            case EMAIL_NOTIFICATION:
                _token1 = base64::encode(notification_t1->get()).c_str();
                _token2 = base64::encode(notification_t2->get()).c_str();
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
