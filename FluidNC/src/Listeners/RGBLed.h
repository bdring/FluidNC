#pragma once

#include "SysListener.h"
#include "../Pin.h"

#include <Adafruit_NeoPixel.h>

namespace Listeners {
    class RGBLed : public SysListener {
        Adafruit_NeoPixel* pixels_ = nullptr;

        Pin pin_;
        int index_ = 0;

        std::string getColor(int value) {
            if (value == -1) {
                return "none";
            } else {
                char buf[16];
                snprintf(buf, 16, "%02X%02X%02X", (value >> 16) & 0xFF, (value >> 8) & 0xff, value & 0xff);
                return buf;
            }
        }

        int parseColor(const std::string& value, int deft) {
            if (value == "none") {  // no change
                return -1;
            }

            if (value.size() != 6) {
                log_warn("Incorrect hex value: " << value);
                return deft;
            }

            int v = 0;
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

        int idle        = 0x007F00;
        int alarm       = 0x7F0000;
        int checkMode   = 0xb936bf;
        int homing      = 0x501f00;
        int cycle       = 0x7f4422;
        int hold        = 0x777744;
        int jog         = 0x007f3f;
        int safetyDoor  = 0x3f7f00;
        int sleep       = 0x001F00;
        int configAlarm = 0x7f0000;

        void handleRGBString(Configuration::HandlerBase& handler, const char* name, int& value) {
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

        const char* name() const override { return "rgbled"; }

        void init() override;
    };
}
