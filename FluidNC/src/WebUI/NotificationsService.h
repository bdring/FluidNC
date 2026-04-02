// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Module.h"  // Module
#include "Settings.h"
#include "WebUI/Authentication.h"

#include <cstdint>

namespace WebUI {
    class NotificationsService : public Module {
    public:
        NotificationsService(const char* name) : Module(name) {
            _started          = false;
            _notificationType = 0;
            _token1           = "";
            _token2           = "";
            _settings         = "";
        }

        static bool        sendMSG(const char* title, const char* message);
        static const char* getTypeString();
        static bool        started();
        static void        configureSettings(EnumSetting* notificationType,
                                             StringSetting* notificationToken1,
                                             StringSetting* notificationToken2,
                                             StringSetting* notificationSettings);
        static Error       sendMessage(const char* parameter, AuthenticationLevel auth_level, Channel& out);

        void init() override;
        void deinit() override;

        ~NotificationsService();

    private:
        static bool        _started;
        static uint8_t     _notificationType;
        static std::string _token1;
        static std::string _token2;
        static std::string _settings;
        static std::string _serveraddress;
        static uint16_t    _port;

        static bool  sendPushoverMSG(const char* title, const char* message);
        static bool  sendEmailMSG(const char* title, const char* message);
        static bool  sendLineMSG(const char* title, const char* message);
        static bool  sendTelegramMSG(const char* title, const char* message);
        static bool  getPortFromSettings();
        static bool  getServerAddressFromSettings();
        static bool  getEmailFromSettings();
    };
}
