// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Copyright (c) 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#ifdef ENABLE_BLUETOOTH
#    include "../Configuration/Configurable.h"
#    include "../Config.h"  // ENABLE_*

#    include <WString.h>
#    include <BluetoothSerial.h>

const char* const DEFAULT_BT_NAME = "FluidNC";

namespace WebUI {
    extern BluetoothSerial SerialBT;

    class BTConfig {
    private:
        static BTConfig* instance;  // BT Callback does not support passing parameters. Sigh.

        String _btclient = "";
        String _btname;
        char   _deviceAddrBuffer[18];

        static void my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);

        //boundaries
    public:
        static const int MAX_BTNAME_LENGTH = 32;
        static const int MIN_BTNAME_LENGTH = 1;

        BTConfig();

        String        info();
        static bool   isBTnameValid(const char* hostname);
        const String& BTname() const { return _btname; }
        const String& client_name() const { return _btclient; }
        const char*   device_address();
        bool          begin();
        void          end();
        void          handle();
        void          reset_settings();
        bool          Is_BT_on() const;

        ~BTConfig();
    };

    extern BTConfig bt_config;
}

#endif
