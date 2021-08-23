#pragma once

/*
  BTConfig.h -  Bluetooth functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.
  Copyright (c) 2021 Stefan de Bruijn. Changed to a class based structure, 
                     fixed multiple bugs with static and added configuration.            

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../Configuration/Configurable.h"
#include "../Config.h"  // ENABLE_*

#include <WString.h>

#ifndef ENABLE_BLUETOOTH
namespace WebUI {
    class BluetoothSerial {
    public:
        BluetoothSerial() = default;
        int read() { return -1; };
        // This is hardwired at 512 because the real BluetoothSerial hardwires
        // the Rx queue size to 512 and code in Report.cpp subtracts available()
        // from that to determine how many characters can be sent.
        int  available() { return 512; };
        void print(const char* text) {};
    };
    extern BluetoothSerial SerialBT;

    class BTConfig : public Configuration::Configurable {
    private:
        String _btname = "";

    public:
        BTConfig() = default;
        void          handle() {}
        bool          begin() { return false; }
        void          end() {}
        bool          Is_BT_on() { return false; }
        String        info() { return String(); }
        const String& BTname() const { return _btname; }
        void          group(Configuration::HandlerBase& handler) override {}
    };
}
#else
#    include <BluetoothSerial.h>

namespace WebUI {
    extern BluetoothSerial SerialBT;

    class BTConfig : public Configuration::Configurable {
    private:
        static BTConfig* instance;  // BT Callback does not support passing parameters. Sigh.

        String _btclient = "";
        String _btname   = "btfluidnc";
        char   _deviceAddrBuffer[18];

        static const int MAX_BTNAME_LENGTH = 32;
        static const int MIN_BTNAME_LENGTH = 1;

        static void my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);

        //boundaries
    public:
        BTConfig();

        void validate() const override {
            Assert(_btname.length() > 0, "Bluetooth must have a name if it's configured");
            Assert(_btname.length() >= MIN_BTNAME_LENGTH && _btname.length() <= MAX_BTNAME_LENGTH,
                   "Bluetooth name must be between %d and %d characters long",
                   MIN_BTNAME_LENGTH,
                   MAX_BTNAME_LENGTH);
        }
        void group(Configuration::HandlerBase& handler) override { handler.item("_name", _btname); }

        String        info();
        bool          isBTnameValid(const char* hostname);
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
}

#endif
