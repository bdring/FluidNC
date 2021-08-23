/*
  BTConfig.cpp -  Bluetooth functions class

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

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

#include "BTConfig.h"

#include "../Machine/MachineConfig.h"
#include "../Report.h"  // CLIENT_*
#include "Commands.h"   // COMMANDS

#include <cstdint>

// SerialBT sends the data over Bluetooth
namespace WebUI {
    BluetoothSerial SerialBT;
}
// The instance variable for the BTConfig class is in config->_comms

#ifdef ENABLE_BLUETOOTH

extern "C" {
const uint8_t* esp_bt_dev_get_address(void);
}

namespace WebUI {
    BTConfig* BTConfig::instance = nullptr;

    BTConfig::BTConfig() {}

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

    String BTConfig::info() {
        String result;
        String tmp;
        if (Is_BT_on()) {
            result += "Mode=BT:Name=";
            result += _btname;
            result += "(";
            result += device_address();
            result += "):Status=";
            if (SerialBT.hasClient()) {
                result += "Connected with " + _btclient;
            } else {
                result += "Not connected";
            }
        } else {
            result += "No BT";
        }
        return result;
    }
    /**
     * Check if BlueTooth string is valid
     */

    bool BTConfig::isBTnameValid(const char* hostname) {
        //limited size
        if (!hostname) {
            return true;
        }
        char c;
        // length is checked automatically by string setting
        //only letter and digit
        for (int i = 0; i < strlen(hostname); i++) {
            c = hostname[i];
            if (!(isdigit(c) || isalpha(c) || c == '_')) {
                return false;
            }
        }
        return true;
    }

    const char* BTConfig::device_address() {
        const uint8_t* point = esp_bt_dev_get_address();
        char*          str   = _deviceAddrBuffer;
        str[17]              = '\0';
        sprintf(
            str, "%02X:%02X:%02X:%02X:%02X:%02X", (int)point[0], (int)point[1], (int)point[2], (int)point[3], (int)point[4], (int)point[5]);
        return str;
    }

    /**
     * begin WiFi setup
     */
    bool BTConfig::begin() {
        instance = this;

        log_debug("Begin");
        //stop active services
        end();

        log_debug("end");
        if (_btname.length()) {
            log_debug("length");
            if (!SerialBT.begin(_btname)) {
                log_debug("name");
                report_status_message(Error::BtFailBegin, CLIENT_ALL);
                return false;
            }
            log_debug("register");
            SerialBT.register_callback(&my_spp_cb);
            log_info("BT Started with " << _btname);
            return true;
        }
        log_info("BT is not enabled");
        end();
        return false;
    }

    /**
     * End WiFi
     */
    void BTConfig::end() { SerialBT.end(); }

    /**
     * Reset Bluetooth settings
     * XXX this is not called from anywhere
     */
    void BTConfig::reset_settings() {
        auto bt = config->_comms->_bluetoothConfig;
        if (bt) {
            delete bt;
            config->_comms->_bluetoothConfig = nullptr;
        }
        log_info("BT reset done");
    }

    /**
     * Check if BT is on and working
     */
    bool BTConfig::Is_BT_on() const { return btStarted(); }

    /**
     * Handle not critical actions that must be done in sync environement
     */
    void BTConfig::handle() {
        //If needed
        COMMANDS::wait(0);
    }

    BTConfig::~BTConfig() { end(); }
}

#endif
