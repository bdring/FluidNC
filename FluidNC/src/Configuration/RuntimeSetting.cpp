// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "RuntimeSetting.h"

#include "../Report.h"
#include "../Protocol.h"  // send_line()

#include <cstdlib>
#include <atomic>

namespace Configuration {
    RuntimeSetting::RuntimeSetting(const char* key, const char* value, Channel& out) : newValue_(value), out_(out) {
        // Remove leading '/' if it is present
        setting_ = (*key == '/') ? key + 1 : key;
        // Also remove trailing '/' if it is present

        start_ = setting_;
        // Read fence for config. Shouldn't be necessary, but better safe than sorry.
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
    }

    std::string RuntimeSetting::setting_prefix() {
        std::string s("$/");
        s += setting_;
        s += "=";
        return s;
    }

    void RuntimeSetting::enterSection(const char* name, Configuration::Configurable* value) {
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
                if (newValue_ == nullptr) {
                    log_to(out_, "/", setting_ << ":");
                    Configuration::Generator generator(out_, 1);
                    value->group(generator);
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
                log_to(out_, "", setting_prefix() << (value ? "true" : "false"));
            } else {
                value = (!strcasecmp(newValue_, "true") || !strcasecmp(newValue_, "yes") || !strcasecmp(newValue_, "1"));
            }
        }
    }

    void RuntimeSetting::item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                log_to(out_, "", setting_prefix() << value);
            } else {
                value = atoi(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, uint32_t& value, uint32_t minValue, uint32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                out_ << "$/" << setting_ << "=" << value << '\n';
            } else {
                if (newValue_[0] == '-') {  // constrain negative values to 0
                    value = 0;
                    log_warn("Negative value not allowed");
                } else
                    value = atoll(newValue_);
            }
            constrain_with_message(value, minValue, maxValue);
        }
    }

    void RuntimeSetting::item(const char* name, float& value, float minValue, float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                // XXX precision
                log_to(out_, "", setting_prefix() << value);
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
                log_to(out_, "", setting_prefix() << value);
            } else {
                value = String(newValue_);
            }
        }
    }

    void RuntimeSetting::item(const char* name, int& value, EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_ == nullptr) {
                for (auto e2 = e; e2->name; ++e2) {
                    if (e2->value == value) {
                        log_to(out_, "", setting_prefix() << e2->name);
                        return;
                    }
                }

            } else {
                if (isdigit(newValue_[0])) {  // if the first char is a number. assume it is an index of a webui enum list
                    int indexVal = 0;
                    indexVal     = atoi(newValue_);
                    for (auto e2 = e; e2->name; ++e2) {
                        if (e2->value == indexVal) {
                            value     = e2->value;
                            newValue_ = e2->name;
                            return;
                        }
                    }
                }
                for (auto e2 = e; e2->name; ++e2) {
                    if (!strcasecmp(newValue_, e2->name)) {
                        value = e2->value;
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
                if (value.size() == 0) {
                    log_to(out_, "None");
                } else {
                    for (speedEntry n : value) {
                        log_to(out_, "", n.speed << "=" << n.percent << "%")
                    }
                }
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
                log_to(out_, "", setting_prefix() << value.toString());
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
                log_to(out_, "", setting_prefix() << value.name());
            } else {
                log_to(out_, "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    RuntimeSetting::~RuntimeSetting() {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);  // Write fence for config
    }
}
