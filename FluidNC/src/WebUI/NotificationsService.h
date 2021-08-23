// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Config.h"  // ENABLE_*

#ifndef ENABLE_WIFI
namespace WebUI {
    class NotificationsService {
    public:
        NotificationsService() = default;
        bool sendMSG(const char* title, const char* message) { return false; };
    };
    extern NotificationsService notificationsservice;
}
#else
#    include <WString.h>
#    include <cstdint>

namespace WebUI {
    class NotificationsService {
    public:
        NotificationsService();

        bool        begin();
        void        end();
        void        handle();
        bool        sendMSG(const char* title, const char* message);
        const char* getTypeString();
        bool        started();

        ~NotificationsService();

    private:
        bool     _started;
        uint8_t  _notificationType;
        String   _token1;
        String   _token2;
        String   _settings;
        String   _serveraddress;
        uint16_t _port;

        bool sendPushoverMSG(const char* title, const char* message);
        bool sendEmailMSG(const char* title, const char* message);
        bool sendLineMSG(const char* title, const char* message);
        bool getPortFromSettings();
        bool getServerAddressFromSettings();
        bool getEmailFromSettings();
    };

    extern NotificationsService notificationsservice;
}

#endif
