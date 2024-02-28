// Copyright (c) 2014 Luc Lebosse. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifdef ENABLE_BLUETOOTH

#    include "BTConfig.h"

#    include "../Machine/MachineConfig.h"
#    include "../Report.h"  // CLIENT_*
#    include "Commands.h"   // COMMANDS
#    include "WebSettings.h"

#    include "esp_bt.h"
#    include "esp_bt_main.h"

#    include <cstdint>

// SerialBT sends the data over Bluetooth
namespace WebUI {
    BTConfig        bt_config __attribute__((init_priority(105)));
    BluetoothSerial SerialBT;
    BTChannel       btChannel;
}
// The instance variable for the BTConfig class is in config->_comms

extern "C" {
const uint8_t* esp_bt_dev_get_address(void);
}

namespace WebUI {
    EnumSetting*   bt_enable;
    BTNameSetting* bt_name;

    size_t BTChannel::write(uint8_t data) {
        static uint8_t lastchar = '\0';
        if (_addCR && data == '\n' && lastchar != '\r') {
            SerialBT.write('\r');
        }
        lastchar = data;
        return SerialBT.write(data);
    }

    BTConfig* BTConfig::instance = nullptr;

    BTConfig::BTConfig() {
        bt_enable = new EnumSetting("Bluetooth Enable", WEBSET, WA, "ESP141", "Bluetooth/Enable", 1, &onoffOptions);

        bt_name = new BTNameSetting("Bluetooth name", "ESP140", "Bluetooth/Name", DEFAULT_BT_NAME);
    }

    void BTConfig::my_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
        auto inst = instance;
        switch (event) {
            case ESP_SPP_SRV_OPEN_EVT: {  //Server connection open
                char str[18];
                str[17]       = '\0';
                uint8_t* addr = param->srv_open.rem_bda;
                sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
                inst->_btclient = str;
                log_info("BT Connected with " << str);
            } break;
            case ESP_SPP_CLOSE_EVT:  //Client connection closed
                log_info("BT Disconnected");
                inst->_btclient = "";
                break;
            default:
                break;
        }
    }

    std::string BTConfig::info() {
        std::string result;
        if (isOn()) {
            result += "Mode=BT:Name=";
            result += _btname.c_str();
            result += "(";
            result += device_address();
            result += "):Status=";
            if (SerialBT.hasClient()) {
                result += "Connected with ";
                result + _btclient.c_str();
            } else {
                result += "Not connected";
            }
        } else {
            result += "No BT";
        }
        return result;
    }
    const char* BTConfig::device_address() {
        const uint8_t* point = esp_bt_dev_get_address();
        char*          str   = _deviceAddrBuffer;
        str[17]              = '\0';
        sprintf(
            str, "%02X:%02X:%02X:%02X:%02X:%02X", (int)point[0], (int)point[1], (int)point[2], (int)point[3], (int)point[4], (int)point[5]);
        return str;
    }

    int BTChannel::available() { return SerialBT.available(); }
    int BTChannel::read() { return SerialBT.read(); }
    int BTChannel::peek() { return SerialBT.peek(); }

    bool BTChannel::realtimeOkay(char c) { return _lineedit->realtime(c); }

    bool BTChannel::lineComplete(char* line, char c) {
        if (_lineedit->step(c)) {
            _linelen        = _lineedit->finish();
            _line[_linelen] = '\0';
            strcpy(line, _line);
            _linelen = 0;
            return true;
        }
        return false;
    }

    Channel* BTChannel::pollLine(char* line) {
        // UART0 is the only Uart instance that can be a channel input device
        // Other UART users like RS485 use it as a dumb character device
        if (_lineedit == nullptr) {
            return nullptr;
        }
        return Channel::pollLine(line);
    }

    void BTConfig::releaseMem() {
        log_debug("Releasing Bluetooth memory");
        esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
        esp_bt_mem_release(ESP_BT_MODE_BLE);
        log_debug("Heap: " << xPortGetFreeHeapSize());
    }

    /**
     * begin WiFi setup
     */
    bool BTConfig::begin() {
        instance = this;

        log_debug("Begin Bluetooth setup");
        //stop active services
        end();

        log_debug("Heap: " << xPortGetFreeHeapSize());
        _btname = bt_name->getStringValue();
        if (bt_enable->get() && _btname.length()) {
            esp_bt_mem_release(ESP_BT_MODE_BLE);
            log_debug("Heap: " << xPortGetFreeHeapSize());
            if (!SerialBT.begin(_btname.c_str())) {
                log_error("Bluetooth failed to start");
                return false;
            }

            SerialBT.register_callback(&my_spp_cb);
            log_info("BT Started with " << _btname);
            allChannels.registration(&btChannel);
            return true;
        }
        releaseMem();
        log_info("BT is not enabled");
        return false;
    }

    /**
     * End WiFi
     */
    void BTConfig::end() {
        if (isOn()) {
            SerialBT.end();
            allChannels.deregistration(&btChannel);
        }
    }

    /**
     * Check if BT is on and working
     */
    bool BTConfig::isOn() const { return btStarted(); }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void BTConfig::handle() {}

    BTConfig::~BTConfig() { end(); }
}

#endif
