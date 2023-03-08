// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Copyright (c) 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#ifndef ENABLE_BLUETOOTH
namespace WebUI {
    class BTConfig {
    public:
        static std::string info() { return std::string(); }

        static bool begin() { return false; };
        static void end() {};
        static void handle() {}
        static bool isOn() { return false; }
    };
    extern BTConfig bt_config;
}
#else
#    include "../Configuration/Configurable.h"
#    include "../Config.h"    // ENABLE_*
#    include "../Settings.h"  // ENABLE_*
#    include "../lineedit.h"

#    include <WString.h>
#    include <BluetoothSerial.h>

const char* const DEFAULT_BT_NAME = "FluidNC";

namespace WebUI {
    extern EnumSetting*   bt_enable;
    extern StringSetting* bt_name;

    extern BluetoothSerial SerialBT;

    class BTChannel : public Channel {
    private:
        Lineedit* _lineedit;

    public:
        // BTChannel(bool addCR = false) : _linelen(0), _addCR(addCR) {}
        BTChannel() : Channel("bluetooth", true) { _lineedit = new Lineedit(this, _line, Channel::maxLine - 1); }
        virtual ~BTChannel() = default;

        int    available() override;
        int    read() override;
        int    peek() override;
        void   flush() override { SerialBT.flush(); }
        size_t write(uint8_t data) override;
        // 512 is RX_QUEUE_SIZE which is defined in BluetoothSerial.cpp but not in its .h
        int rx_buffer_available() override { return 512 - SerialBT.available(); }

        bool realtimeOkay(char c) override;
        bool lineComplete(char* line, char c) override;

        Channel* pollLine(char* line) override;
    };
    extern BTChannel btChannel;

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

        std::string info();

        static bool   isBTnameValid(const char* hostname);
        const String& BTname() const { return _btname; }
        const String& client_name() const { return _btclient; }
        const char*   device_address();
        bool          begin();
        void          end();
        void          handle();
        void          reset_settings();
        bool          isOn() const;

        ~BTConfig();
    };

    extern BTConfig bt_config;

    extern EnumSetting*   bt_enable;
    extern StringSetting* bt_name;
}

#endif
