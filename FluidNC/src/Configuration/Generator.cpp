// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Generator.h"

#include "Configurable.h"

#include <cstring>
#include <cstdio>
#include <atomic>

namespace Configuration {
    Generator::Generator(Channel& dst, int indent) : indent_(indent), dst_(dst) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
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

}
