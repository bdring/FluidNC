/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "JsonGenerator.h"

#include "Configurable.h"

#include <cstring>
#include <cstdio>
#include <atomic>

namespace Configuration {
    JsonGenerator::JsonGenerator(WebUI::JSONencoder& encoder) : _encoder(encoder) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);

        _currentPath[0] = '\0';
        _depth          = 0;
        _paths[0]       = _currentPath;
    }

    void JsonGenerator::enter(const char* name) {
        auto currentEnd = _paths[_depth];
        *currentEnd++   = '/';
        for (auto i = name; *i;) {
            Assert(currentEnd != _currentPath + 256, "Path out of bounds while serializing json.");
            *currentEnd++ = *i++;
        }
        ++_depth;
        _paths[_depth] = currentEnd;
        *currentEnd    = '\0';
    }

    void JsonGenerator::add(Configuration::Configurable* configurable) {
        if (configurable != nullptr) {
            configurable->group(*this);
        }
    }

    void JsonGenerator::leave() {
        --_depth;
        Assert(_depth >= 0, "Depth out of bounds while serializing to json");
        *_paths[_depth] = '\0';
    }

    void JsonGenerator::enterSection(const char* name, Configurable* value) {
        enter(name);
        value->group(*this);
        leave();
    }

    void JsonGenerator::item(const char* name, bool& value) {
        enter(name);
        const char* val = value ? "Yes" : "No";
        _encoder.begin_webui(name, _currentPath, "B", val);
        _encoder.begin_array("O");
        {
            _encoder.begin_object();
            _encoder.member("No", 0);
            _encoder.member("Yes", 1);
            _encoder.end_object();
        }
        _encoder.end_array();
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, int& value, int32_t minValue, int32_t maxValue) {
        enter(name);
        char buf[32];
        itoa(value, buf, 10);
        _encoder.begin_webui(name, _currentPath, "I", buf, minValue, maxValue);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, float& value, float minValue, float maxValue) {
        enter(name);
        // WebUI does not explicitly recognize the R type, but nevertheless handles it correctly.
        _encoder.begin_webui(name, _currentPath, "R", String(value, 3).c_str());
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, std::vector<speedEntry>& value) {}
    void JsonGenerator::item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
        // Not sure if I should comment this out or not. The implementation is similar to the one in Generator.h.
    }

    void JsonGenerator::item(const char* name, String& value, int minLength, int maxLength) {
        enter(name);
        _encoder.begin_webui(name, _currentPath, "S", value.c_str(), minLength, maxLength);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, Pin& value) {
        // We commented this out, because pins are very confusing for users. The code is correct,
        // but it really gives more support than it's worth.
        /*
        enter(name);
        auto sv = value.name();
        _encoder.begin_webui(name, _currentPath, "S", sv.c_str(), 0, 255);
        _encoder.end_object();
        leave();
        */
    }

    void JsonGenerator::item(const char* name, IPAddress& value) {
        enter(name);
        _encoder.begin_webui(name, _currentPath, "A", value.toString().c_str());
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, int& value, EnumItem* e) {
        enter(name);
        const char* str = "unknown";
        for (; e->name; ++e) {
            if (value == e->value) {
                str = e->name;
                break;
            }
        }

        _encoder.begin_webui(name, _currentPath, "B", str);
        _encoder.begin_array("O");
        for (; e->name; ++e) {
            _encoder.begin_object();
            _encoder.member(e->name, e->value);
            _encoder.end_object();
        }
        _encoder.end_array();
        _encoder.end_object();
        leave();
    }
}
