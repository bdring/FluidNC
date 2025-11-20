// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "RuntimeSetting.h"

#include "Report.h"
#include "Protocol.h"  // send_line()
#include "string_util.h"
#include "Machine/Axes.h"  // axisType
#include "Parameters.h"    // read_number

#include <cstdlib>
#include <atomic>

namespace Configuration {
    RuntimeSetting::RuntimeSetting(std::string_view key, std::string_view value, Channel& out) :
        setting_(key), newValue_(value), out_(out) {
        // Remove leading '/' if it is present
        if (setting_.front() == '/') {
            setting_.remove_prefix(1);
        }
        // Also remove trailing '/' if it is present
        if (setting_.back() == '/') {
            setting_.remove_suffix(1);
        }

        start_ = setting_;
        // Read fence for config. Shouldn't be necessary, but better safe than sorry.
        std::atomic_thread_fence(std::memory_order_seq_cst);
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
            std::string_view residue;
            string_util::split(start_, residue, '/');

            if (residue.empty()) {
                if (newValue_.empty()) {
                    log_stream(out_, "/" << setting_ << ":");
                    Configuration::Generator generator(out_, 1);
                    value->group(generator);
                    isHandled_ = true;
                } else {
                    log_error("Can't set a value on a section");
                }
            } else {
                // Recurse to handle child nodes
                start_ = residue;
                value->group(*this);
            }

            // Restore situation:
            start_ = previous;
        }
    }

    void RuntimeSetting::item(const char* name, bool& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << (value ? "true" : "false"));
            } else if (string_util::equal_ignore_case(newValue_, "true") || string_util::equal_ignore_case(newValue_, "yes") ||
                       string_util::equal_ignore_case(newValue_, "on")) {
                value = true;
            } else if (string_util::equal_ignore_case(newValue_, "false") || string_util::equal_ignore_case(newValue_, "no") ||
                       string_util::equal_ignore_case(newValue_, "off")) {
                value = false;
            } else {
                float fvalue;
                if (!read_number(newValue_, fvalue)) {
                    log_error("Bad numeric value");
                } else {
                    value = fvalue;
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, int32_t& value, int32_t minValue, int32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value);
            } else {
                float fvalue;
                if (read_number(newValue_, fvalue)) {
                    value = static_cast<int32_t>(fvalue);
                    constrain_with_message(value, minValue, maxValue);
                } else {
                    log_error("Bad numeric value");
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, uint32_t& value, uint32_t minValue, uint32_t maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value);
            } else {
                float fvalue;
                if (read_number(newValue_, fvalue)) {
                    if (fvalue < 0) {
                        log_warn("Negative value not allowed");
                        fvalue = 0;
                    }
                    value = static_cast<uint32_t>(fvalue);
                    constrain_with_message(value, minValue, maxValue);
                } else {
                    log_error("Bad numeric value");
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, float& value, const float minValue, const float maxValue) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                // XXX precision
                log_stream(out_, setting_prefix() << value);
            } else {
                if (read_number(newValue_, value)) {
                    constrain_with_message(value, minValue, maxValue);
                } else {
                    log_error("Bad numeric value");
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, UartData& wordLength, UartParity& parity, UartStop& stopBits) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << encodeUartMode(wordLength, parity, stopBits));
            } else {
                const char* errstr = decodeUartMode(newValue_, wordLength, parity, stopBits);
                if (*errstr) {
                    log_error_to(out_, errstr);
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, std::string& value, const int minLength, const int maxLength) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value);
            } else {
                value = newValue_;
            }
        }
    }

    void RuntimeSetting::item(const char* name, uint32_t& value, const EnumItem* e) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                for (auto e2 = e; e2->name; ++e2) {
                    if (e2->value == value) {
                        log_stream(out_, setting_prefix() << e2->name);
                        return;
                    }
                }

            } else {
                if (isdigit(newValue_.front())) {  // if the first char is a number. assume it is an index of a webui enum list
                    int32_t indexVal = 0;
                    string_util::from_decimal(newValue_, indexVal);
                    for (auto e2 = e; e2->name; ++e2) {
                        if (e2->value == indexVal) {
                            value     = e2->value;
                            newValue_ = e2->name;
                            return;
                        }
                    }
                }
                for (auto e2 = e; e2->name; ++e2) {
                    if (string_util::equal_ignore_case(newValue_, e2->name)) {
                        value = e2->value;
                        return;
                    }
                }

                if (newValue_.empty()) {
                    value = e->value;
                    return;
                } else {
                    Assert(false, "Provided enum value %s is not valid", newValue_);
                }
            }
        }
    }

    void RuntimeSetting::item(const char* name, axis_t& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << Machine::Axes::axisName(value));
                return;
            }
            if (isdigit(newValue_.front())) {  // if the first char is a number. assume it is an index of a webui enum list
                int32_t indexVal = 0;
                string_util::from_decimal(newValue_, indexVal);
                value     = static_cast<axis_t>(indexVal);
                newValue_ = Machine::Axes::axisName(value);
                return;
            }
            axis_t axis = Machine::Axes::axisNum(newValue_);
            if (axis != INVALID_AXIS) {
                value = axis;
                return;
            }

            Assert(false, "Invalid axis name", newValue_);
        }
    }

    void RuntimeSetting::item(const char* name, std::vector<speedEntry>& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                if (value.size() == 0) {
                    log_string(out_, "None");
                } else {
                    LogStream msg(out_, "");
                    msg << setting_prefix();
                    const char* separator = "";
                    for (speedEntry n : value) {
                        msg << separator << n.speed << "=" << setprecision(2) << n.percent << "%";
                        separator = " ";
                    }
                    // The destructor sends the line when msg goes out of scope
                }
            } else {
                // It is distasteful to have this code that essentially duplicates
                // Parser.cpp speedEntryValue(), albeit using std::string instead of
                // StringRange.  It might be better to have a single std::string version,
                // then pass it StringRange.str()
                std::string_view        newStr(newValue_);
                std::vector<speedEntry> smValue;
                while (!newStr.empty()) {
                    speedEntry       entry;
                    std::string_view entryStr;
                    auto             i = newStr.find(' ');
                    if (i != std::string::npos) {
                        entryStr = newStr.substr(0, i);
                        newStr   = newStr.substr(i + 1);
                    } else {
                        entryStr = newStr;
                        newStr   = "";
                    }
                    std::string_view speed;
                    i = entryStr.find('=');
                    Assert(i != std::string::npos, "Bad speed map entry");
                    string_util::from_decimal(entryStr.substr(0, i), entry.speed);
                    string_util::from_float(entryStr.substr(i + 1), entry.percent);
                    smValue.push_back(entry);
                }
                value = smValue;
            }
        }
    }

    void RuntimeSetting::item(const char* name, std::vector<float>& value) {
        if (is(name)) {
            LogStream msg(out_, "");
            isHandled_ = true;
            if (newValue_.empty()) {
                if (value.size() == 0) {
                    out_ << "None";
                } else {
                    String separator = "";
                    for (float n : value) {
                        out_ << separator.c_str();
                        out_ << n;
                        separator = " ";
                    }
                }
                msg << '\n';
            } else {
                // It is distasteful to have this code that essentially duplicates
                // Parser.cpp speedEntryValue(), albeit using String instead of
                // StringRange.  It would be better to have a single String version,
                // then pass it StringRange.str()
                auto               newStr = newValue_;
                std::vector<float> smValue;
                while (newStr = string_util::trim(newStr), newStr.length()) {
                    float            entry;
                    std::string_view entryStr;
                    auto             pos = newStr.find(' ');
                    if (pos == std::string_view::npos) {
                        entryStr = newStr;
                        newStr   = "";
                    } else {
                        entryStr = newStr.substr(0, pos);
                        newStr   = newStr.substr(pos + 1);
                    }
                    bool res = string_util::from_float(entryStr, entry);
                    Assert(res == true, "Bad float value");

                    smValue.push_back(entry);
                }
                value = smValue;

                if (!value.size()) {
                    log_info("Using default value");
                }
                return;
            }
        }
    }

    void RuntimeSetting::item(const char* name, IPAddress& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << IP_string(value));
            } else {
                String ipStr = std::string(newValue_).c_str();
                Assert(value.fromString(ipStr), "Expected an IP address like 192.168.0.100");
            }
        }
    }

    void RuntimeSetting::item(const char* name, EventPin& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value.name());
            } else {
                log_string(out_, "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    void RuntimeSetting::item(const char* name, InputPin& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value.name());
            } else {
                log_string(out_, "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    void RuntimeSetting::item(const char* name, Pin& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value.name());
            } else {
                log_string(out_, "Runtime setting of Pin objects is not supported");
                // auto parsed = Pin::create(newValue);
                // value.swap(parsed);
            }
        }
    }

    void RuntimeSetting::item(const char* name, Macro& value) {
        if (is(name)) {
            isHandled_ = true;
            if (newValue_.empty()) {
                log_stream(out_, setting_prefix() << value.get());
            } else {
                value.set(newValue_);
            }
        }
    }

    RuntimeSetting::~RuntimeSetting() {
        std::atomic_thread_fence(std::memory_order_seq_cst);  // Write fence for config
    }
}
