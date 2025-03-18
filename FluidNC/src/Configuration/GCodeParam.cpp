// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "GCodeParam.h"

#include <cstdlib>

namespace Configuration {
    GCodeParam::GCodeParam(const char* key, float& iovalue, bool get) : _iovalue(iovalue), _get(get) {
        // Remove leading '/' if it is present
        setting_ = (*key == '/') ? key + 1 : key;
        // Also remove trailing '/' if it is present

        start_ = setting_;
    }

    void GCodeParam::error() {
        Assert(false, "Non-numeric config item");
    }

    void GCodeParam::enterSection(const char* name, Configuration::Configurable* value) {
        if (is(name) && !isHandled_) {
            auto previous = start_;

            // Figure out next node
            auto next = start_;
            for (; *next && *next != '/'; ++next) {}

            // Do we have a child?
            if (*next == '/' && next[1] != '\0') {
                ++next;
                start_ = next;
                // Handle child:
                value->group(*this);
            } else {
                error();
            }

            // Restore situation:
            start_ = previous;
        }
    }

    void GCodeParam::item(const char* name, bool& value) {
        if (is(name)) {
            isHandled_ = true;
            if (_get) {
                _iovalue = value;
            } else {
                value = _iovalue;
            }
        }
    }

    void GCodeParam::item(const char* name, int32_t& value, const int32_t minValue, const int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (_get) {
                _iovalue = value;
            } else {
                value = _iovalue;
            }
        }
    }

    void GCodeParam::item(const char* name, uint32_t& value, const uint32_t minValue, const uint32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (_get) {
                _iovalue = value;
            } else {
                if (_iovalue < 0) {  // constrain negative values to 0
                    error();
                } else {
                    value = _iovalue;
                }
                constrain_with_message(value, minValue, maxValue);
            }
        }
    }

    void GCodeParam::item(const char* name, float& value, const float minValue, const float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (_get) {
                _iovalue = value;
            } else {
                value = _iovalue;
                constrain_with_message(value, minValue, maxValue);
            }
        }
    }

    void GCodeParam::item(const char* name, std::string& value, const int minLength, const int maxLength) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, int& value, const EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (_get) {
                _iovalue = value;
            } else {
                value = _iovalue;
            }
        }
    }

    void GCodeParam::item(const char* name, std::vector<speedEntry>& value) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, std::vector<float>& value) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, IPAddress& value) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, EventPin& value) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, Pin& value) {
        if (is(name)) {
            error();
        }
    }

    void GCodeParam::item(const char* name, Macro& value) {
        if (is(name)) {
            error();
        }
    }

    GCodeParam::~GCodeParam() {}
}
