// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

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
                _sendf(out_->client(), "$%s=%s\r\n", setting_, value ? "true" : "false");
            } else {
                value = (!strcasecmp(newValue_, "true"));
            }
        }
    }

    void RuntimeSetting::item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=%d\r\n", setting_, value);
            } else {
                value = atoi(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, float& value, float minValue, float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=%.3f\r\n", setting_, value);
            } else {
                char* floatEnd;
                value = strtof(newValue_, &floatEnd);
            }
        }
    }

    void RuntimeSetting::item(const char* name, String& value, int minLength, int maxLength) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=%s\r\n", setting_, value.c_str());
            } else {
                value = String(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, int& value, EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                for (; e->name; ++e) {
                    if (e->value == value) {
                        _sendf(out_->client(), "$%s=%s\r\n", setting_, e->name);
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

    void RuntimeSetting::item(const char* name, std::vector<speedEntry>& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=", setting_);
                if (value.size() == 0) {
                    _sendf(out_->client(), "None");
                } else {
                    for (speedEntry n : value) {
                        _sendf(out_->client(), " %d=%.3f%%", n.speed, n.percent);
                    }
                }
                _sendf(out_->client(), "\n");
            } else {
                // It is distasteful to have this code that essentially duplicates
                // Parser.cpp speedEntryValue(), albeit using String instead of
                // StringRange.  It would be better to have a single String version,
                // then pass it StringRange.str()
                auto                    newStr = String(newValue_);
                std::vector<speedEntry> smValue;
                while (newStr.trim(), newStr.length()) {
                    speedEntry entry;
                    String     entryStr;
                    auto       i = newStr.indexOf(' ');
                    if (i >= 0) {
                        entryStr = newStr.substring(0, i);
                        newStr   = newStr.substring(i + 1);
                    } else {
                        entryStr = newStr;
                        newStr   = "";
                    }
                    String speed;
                    i = entryStr.indexOf('=');
                    Assert(i > 0, "Bad speed map entry");
                    entry.speed   = entryStr.substring(0, i).toInt();
                    entry.percent = entryStr.substring(i + 1).toFloat();
                    smValue.push_back(entry);
                }
                value = smValue;
            }
        }
    }

    void RuntimeSetting::item(const char* name, IPAddress& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=%s\r\n", setting_, value.toString().c_str());
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
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                _sendf(out_->client(), "$%s=%s\r\n", setting_, value.name().c_str());
            } else {
                _sendf(out_->client(), "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    RuntimeSetting::~RuntimeSetting() {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // Write fence for config
    }
}
