// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "JsonGenerator.h"

#include "Configurable.h"
#include "Machine/Axes.h"  // Axes

#include <cstring>
#include <cstdio>
#include <atomic>
#include <sstream>
#include <iomanip>

namespace Configuration {
    JsonGenerator::JsonGenerator(JSONencoder& encoder) : _encoder(encoder) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void JsonGenerator::enter(const char* name) {
        _path_lengths.push(_currentPath.length());
        _currentPath += "/";
        _currentPath += name;
    }

    void JsonGenerator::add(Configuration::Configurable* configurable) {
        if (configurable != nullptr) {
            configurable->group(*this);
        }
    }

    void JsonGenerator::leave() {
        Assert(!_path_lengths.empty(), "Depth out of bounds while serializing to json");
        _currentPath.erase(_path_lengths.top());
        _path_lengths.pop();
    }

    void JsonGenerator::enterSection(const char* name, Configurable* value) {
        enter(name);
        value->group(*this);
        leave();
    }

    void JsonGenerator::item(const char* name, bool& value) {
        enter(name);
        const char* val = value ? "1" : "0";
        _encoder.begin_webui(_currentPath, "B", val);
        _encoder.begin_array("O");
        {
            _encoder.begin_object();
            _encoder.member("False", int32_t(0));
            _encoder.end_object();
            _encoder.begin_object();
            _encoder.member("True", int32_t(1));
            _encoder.end_object();
        }
        _encoder.end_array();
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) {
        enter(name);
        char buf[32];
        itoa(value, buf, 10);
        _encoder.begin_webui(_currentPath, "I", buf, minValue, maxValue);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) {
        enter(name);
        char buf[32];
        snprintf(buf, 32, "%u", static_cast<unsigned int>(value));
        _encoder.begin_webui(_currentPath, "I", buf, minValue, maxValue);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, float& value, const float minValue, const float maxValue) {
        enter(name);
        // WebUI does not explicitly recognize the R type, but nevertheless handles it correctly.
        if (value > 999999.999f) {
            value = 999999.999f;
        } else if (value < -999999.999f) {
            value = -999999.999f;
        }
        std::ostringstream fstr;
        fstr << std::fixed << std::setprecision(3) << value;
        _encoder.begin_webui(_currentPath, "R", fstr.str());
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, std::vector<speedEntry>& value) {}
    void JsonGenerator::item(const char* name, std::vector<float>& value) {}

    void JsonGenerator::item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
        enter(name);
        auto value = encodeUartMode(wordLength, parity, stopBits);
        _encoder.begin_webui(_currentPath, "S", value.c_str(), 3, 5);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, std::string& value, const int minLength, const int maxLength) {
        enter(name);
        _encoder.begin_webui(_currentPath, "S", value.c_str(), minLength, maxLength);
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, Macro& value) {
        enter(name);
        _encoder.begin_webui(_currentPath, "S", value.get().c_str(), 0, 255);
        _encoder.end_object();
        leave();
    }
    void JsonGenerator::item(const char* name, Pin& value) {
        // We commented this out, because pins are very confusing for users. The code is correct,
        // but it really gives more support than it's worth.
        /*
        enter(name);
        auto sv = value.name();
        _encoder.begin_webui(_currentPath, "S", sv.c_str(), 0, 255);
        _encoder.end_object();
        leave();
        */
    }

    void JsonGenerator::item(const char* name, EventPin& value) {
        // We commented this out, because pins are very confusing for users. The code is correct,
        // but it really gives more support than it's worth.
        /*
        enter(name);
        auto sv = value.name();
        _encoder.begin_webui(_currentPath, "S", sv.c_str(), 0, 255);
        _encoder.end_object();
        leave();
        */
    }
    void JsonGenerator::item(const char* name, InputPin& value) {
        // We commented this out, because pins are very confusing for users. The code is correct,
        // but it really gives more support than it's worth.
        /*
        enter(name);
        auto sv = value.name();
        _encoder.begin_webui(_currentPath, "S", sv.c_str(), 0, 255);
        _encoder.end_object();
        leave();
        */
    }

    void JsonGenerator::item(const char* name, IPAddress& value) {
        enter(name);
        _encoder.begin_webui(_currentPath, "A", IP_string(value));
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, uint32_t& value, const EnumItem* e) {
        enter(name);
        int32_t selected_val = 0;
        //const char* str          = "unknown";
        for (auto e2 = e; e2->name; ++e2) {
            if (value == e2->value) {
                //str          = e2->name;
                selected_val = e2->value;
                break;
            }
        }

        _encoder.begin_webui(_currentPath, "B", selected_val);
        _encoder.begin_array("O");
        for (auto e2 = e; e2->name; ++e2) {
            _encoder.begin_object();
            _encoder.member(e2->name, e2->value);
            _encoder.end_object();
        }
        _encoder.end_array();
        _encoder.end_object();
        leave();
    }

    void JsonGenerator::item(const char* name, axis_t& value) {
        enter(name);
        axis_t selected_val = value;

        _encoder.begin_webui(_currentPath, "B", selected_val);
        _encoder.begin_array("O");
        for (axis_t axis = X_AXIS; axis < MAX_N_AXIS; axis++) {
            _encoder.begin_object();
            _encoder.member(Machine::Axes::axisName(axis), axis);
            _encoder.end_object();
        }
        _encoder.end_array();
        _encoder.end_object();
        leave();
    }

}
