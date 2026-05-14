#pragma once

#include "SysListener.h"
#include "../Pin.h"

#include <Adafruit_NeoPixel.h>

namespace Listeners {
    class RGBLed : public SysListener {
        Adafruit_NeoPixel* pixels_ = nullptr;

        Pin      pin_;
        uint32_t index_ = 0;

        std::string getColor(int32_t value) {
            if (value == -1) {
                return "none";
            } else {
                char buf[16];
                snprintf(buf, 16, "%02X%02X%02X", (value >> 16) & 0xFF, (value >> 8) & 0xff, value & 0xff);
                return buf;
            }
        }

        int32_t parseColor(const std::string& value, int32_t deft) {
            if (value == "none") {  // no change
                return -1;
            }

            if (value.size() != 6) {
                log_warn("Incorrect hex value: " << value);
                return deft;
            }

            int32_t v = 0;
            for (int i = 0; i < 3; ++i) {
                int x = 0;
                for (int j = 0; j < 2; ++j) {
                    char c = value[i * 2 + j];
                    if (c >= '0' && c <= '9') {
                        x = x * 16 + c - '0';
                    } else if (c >= 'a' && c <= 'f') {
                        x = x * 16 + c - 'a' + 10;
                    } else if (c >= 'A' && c <= 'F') {
                        x = x * 16 + c - 'A' + 10;
                    } else {
                        log_warn("Incorrect hex value: " << value);
                        return deft;
                    }
                }
                v = (v << 8) + x;
            }
            return v;
        }

        void handleChangeDetail(SystemDirty changes, const system_t& state);

        static void handleChange(SystemDirty changes, const system_t& state, void* userData) {
            static_cast<RGBLed*>(userData)->handleChangeDetail(changes, state);
        }

        uint32_t idle        = 0x007F00;
        uint32_t alarm       = 0x7F0000;
        uint32_t checkMode   = 0xb936bf;
        uint32_t homing      = 0x501f00;
        uint32_t cycle       = 0x7f4422;
        uint32_t hold        = 0x777744;
        uint32_t jog         = 0x007f3f;
        uint32_t safetyDoor  = 0x3f7f00;
        uint32_t sleep       = 0x001F00;
        uint32_t configAlarm = 0x7f0000;

        void handleRGBString(Configuration::HandlerBase& handler, const char* name, uint32_t& value) {
            auto        old = value;
            std::string str = getColor(old);
            handler.item(name, str);
            value = parseColor(str, old);
        }

    public:
        RGBLed();

        virtual void group(Configuration::HandlerBase& handler) override {
            handler.item("pin", pin_);
            handler.item("index", index_);

            handleRGBString(handler, "idle", idle);
            handleRGBString(handler, "alarm", alarm);
            handleRGBString(handler, "checkMode", checkMode);
            handleRGBString(handler, "homing", homing);
            handleRGBString(handler, "cycle", cycle);
            handleRGBString(handler, "hold", hold);
            handleRGBString(handler, "jog", jog);
            handleRGBString(handler, "safetyDoor", safetyDoor);
            handleRGBString(handler, "sleep", sleep);
            handleRGBString(handler, "configAlarm", configAlarm);
        }

        void init() override;
    };
}
