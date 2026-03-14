// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "NotificationsService.h"
#include "Module.h"

namespace WebUI {
    namespace {
        const enum_opt_t notificationOptions = {
            { "NONE", 0 }, { "LINE", 3 }, { "PUSHOVER", 1 }, { "EMAIL", 2 }, { "TG", 4 },
        };

        EnumSetting* notification_type = nullptr;
        StringSetting* notification_t1 = nullptr;
        StringSetting* notification_t2 = nullptr;
        StringSetting* notification_ts = nullptr;

        Error showSetNotification(const char* parameter, AuthenticationLevel auth_level, Channel& out) {  // ESP610
            (void)auth_level;
            if (notification_type == nullptr || notification_t1 == nullptr || notification_t2 == nullptr || notification_ts == nullptr) {
                return Error::InvalidValue;
            }
            if (*parameter == '\0') {
                log_stream(out, notification_type->getStringValue() << " " << notification_ts->getStringValue());
                return Error::Ok;
            }
            std::string s;

            if (!get_param(parameter, "type=", s)) {
                return Error::InvalidValue;
            }
            Error err = notification_type->setStringValue(s);
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
    }
}

namespace {
    struct NotificationsServiceBootstrap {
        NotificationsServiceBootstrap() {
            using namespace WebUI;

            notification_ts = new StringSetting(
                "Notification Settings", WEBSET, WA, NULL, "Notification/TS", "", 0, 127);
            notification_t2 = new StringSetting("Notification Token 2", WEBSET, WA, NULL, "Notification/T2", "", 0, 63);
            notification_t1 = new StringSetting("Notification Token 1", WEBSET, WA, NULL, "Notification/T1", "", 0, 63);
            notification_type = new EnumSetting("Notification type", WEBSET, WA, NULL, "Notification/Type", 0, &notificationOptions);

            NotificationsService::configureSettings(notification_type, notification_t1, notification_t2, notification_ts);
            new WebCommand(
                "TYPE=NONE|PUSHOVER|EMAIL|LINE T1=token1 T2=token2 TS=settings", WEBCMD, WA, "ESP610", "Notification/Setup", showSetNotification);
            new WebCommand("message", WEBCMD, WU, "ESP600", "Notification/Send", NotificationsService::sendMessage);
        }
    } notificationsServiceBootstrap;

    ModuleFactory::InstanceBuilder<WebUI::NotificationsService> __attribute__((init_priority(110))) notification_module("notifications", true);
}
