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

#include "RuntimeSetting.h"

#include "../Report.h"

#include <cstdlib>
#include <atomic>

namespace Configuration {
    RuntimeSetting::RuntimeSetting(const char* key, const char* value, WebUI::ESPResponseStream* out) : newValue_(value), out_(out) {
        // Remove leading '/' if it is present
        setting_ = (*key == '/') ? key + 1 : key;
        start_   = setting_;
        // Read fence for config. Shouldn't be necessary, but better safe than sorry.
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
    }

    void RuntimeSetting::enterSection(const char* name, Configuration::Configurable* value) {
        if (is(name) && !isHandled_) {
            auto previous = start_;

            // Figure out next node
            auto next = start_;
            for (; *next && *next != '/'; ++next) {}

            // Do we have a child?
            if (*next == '/') {
                ++next;
                start_ = next;

                // Handle child:
                value->group(*this);
            } else {
                if (newValue_ == nullptr) {
                    ClientStream ss(CLIENT_ALL);
                    ss << dataBeginMarker;
                    ss << setting_ << ":\n";
                    Configuration::Generator generator(ss, 1);
                    value->group(generator);
                    ss << dataEndMarker;
                    isHandled_ = true;
                } else {
                    log_error("Can't set a value on a section");
                }
            }

            // Restore situation:
            start_ = previous;
        }
    }

    void RuntimeSetting::item(const char* name, bool& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%s\r\n", setting_, value ? "true" : "false");
            } else {
                value = (!strcasecmp(newValue_, "true"));
            }
        }
    }

    void RuntimeSetting::item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%d\r\n", setting_, value);
            } else {
                value = atoi(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, float& value, float minValue, float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%.3f\r\n", setting_, value);
            } else {
                char* floatEnd;
                value = strtof(newValue_, &floatEnd);
            }
        }
    }

    void RuntimeSetting::item(const char* name, StringRange& value, int minLength, int maxLength) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%s\r\n", setting_, value.str().c_str());
            } else {
                value = StringRange(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, int& value, EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                for (; e->name; ++e) {
                    if (e->value == value) {
                        grbl_sendf(out_->client(), "$%s=%s\r\n", setting_, e->name);
                        return;
                    }
                }
            } else {
                for (; e->name; ++e) {
                    if (!strcasecmp(newValue_, e->name)) {
                        value = e->value;
                        return;
                    }
                }

                if (strlen(newValue_) == 0) {
                    value = e->value;
                    return;
                } else {
                    Assert(false, "Provided enum value %s is not valid", newValue_);
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, IPAddress& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%s\r\n", setting_, value.toString().c_str());
            } else {
                IPAddress ip;
                if (!ip.fromString(newValue_)) {
                    Assert(false, "Expected an IP address like 192.168.0.100");
                }
                value = ip;
            }
        }
    }

    void RuntimeSetting::item(const char* name, Pin& value) {
        /*
        Runtime settings of PIN objects is NOT supported!

        if (is(name)) {
            if (newValue_ == nullptr) {
                grbl_sendf(out_->client(), "$%s=%s\r\n", setting_, value.name().c_str());
            } else {
                auto parsed = Pin::create(StringRange(this->value()));
                value.swap(parsed);
            }
        }
        */
    }

    RuntimeSetting::~RuntimeSetting() {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // Write fence for config
    }
}
