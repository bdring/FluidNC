// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <vector>

#include "../Pin.h"
#include "../Report.h"    // report_gcode_modes()
#include "../Protocol.h"  // send_line()
#include "HandlerBase.h"

namespace Configuration {
    class Configurable;

    class Generator : public HandlerBase {
        Generator(const Generator&) = delete;
        Generator& operator=(const Generator&) = delete;

        int      indent_;
        Channel& dst_;
        bool     lastIsNewline_ = false;

        void enter(const char* name);
        void add(Configuration::Configurable* configurable);
        void leave();

    protected:
        void        enterSection(const char* name, Configurable* value) override;
        bool        matchesUninitialized(const char* name) override { return false; }
        HandlerType handlerType() override { return HandlerType::Generator; }

    public:
        Generator(Channel& dst, int indent = 0);

        void send_item(const char* name, const std::string& value) {
            LogStream s(dst_, "");
            lastIsNewline_ = false;
            for (int i = 0; i < indent_ * 2; ++i) {
                s << ' ';
            }
            s << name;
            s << ": ";
            s << value;
        }

        void item(const char* name, int& value, int32_t minValue, int32_t maxValue) override { send_item(name, std::to_string(value)); }

        void item(const char* name, uint32_t& value, uint32_t minValue, uint32_t maxValue) override {
            send_item(name, std::to_string(value));
        }

        void item(const char* name, float& value, float minValue, float maxValue) override { send_item(name, std::to_string(value)); }

        void item(const char* name, std::vector<speedEntry>& value) {
            std::string s;
            if (value.size() == 0) {
                s += "None";
            } else {
                const char* separator = "";
                for (speedEntry n : value) {
                    s += separator;
                    separator = " ";
                    s += std::to_string(n.speed);
                    s += '=';
                    s += std::to_string(n.percent);
                    s += '%';
                }
            }
            send_item(name, s);
        }

        void item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) override {
            std::string s;
            s += std::to_string(int(wordLength) - int(UartData::Bits5) + 5);
            switch (parity) {
                case UartParity::Even:
                    s += 'E';
                    break;
                case UartParity::Odd:
                    s += 'O';
                    break;
                case UartParity::None:
                    s += 'N';
                    break;
            }
            switch (stopBits) {
                case UartStop::Bits1:
                    s += '1';
                    break;
                case UartStop::Bits1_5:
                    s += "1.5";
                    break;
                case UartStop::Bits2:
                    s += '2';
                    break;
            }
            send_item(name, s);
        }

        void item(const char* name, String& value, int minLength, int maxLength) override { send_item(name, value.c_str()); }

        void item(const char* name, bool& value) override { send_item(name, value ? "true" : "false"); }

        void item(const char* name, Pin& value) override { send_item(name, value.name().c_str()); }

        void item(const char* name, IPAddress& value) override { send_item(name, value.toString().c_str()); }
        void item(const char* name, int& value, EnumItem* e) override {
            const char* str = "unknown";
            for (; e->name; ++e) {
                if (value == e->value) {
                    str = e->name;
                    break;
                }
            }
            send_item(name, str);
        }
    };
}
