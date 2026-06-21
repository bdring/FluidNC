// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Generator.h"

#include "Configurable.h"
#include "Machine/Axes.h"  // Axes

#include <cstring>
#include <cstdio>
#include <atomic>

namespace Configuration {
    Generator::Generator(Channel& dst, int_fast8_t indent) : indent_(indent), dst_(dst) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void Generator::enter(const char* name) {
        send_item(name, "");
        indent_++;
    }

    void Generator::add(Configuration::Configurable* configurable) {
        if (configurable != nullptr) {
            configurable->group(*this);
        }
    }

    void Generator::leave() {
        if (!lastIsNewline_) {
            log_string(dst_, "");
            lastIsNewline_ = true;
        }

        indent_--;
    }

    void Generator::enterSection(const char* name, Configurable* value) {
        enter(name);
        value->group(*this);
        leave();
    }

    void Generator::item(const char* name, axis_t& value) {
        send_item(name, Machine::Axes::axisName(value));
    }

}
