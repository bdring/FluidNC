// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Validator.h"

#include "Configurable.h"
#include "System.h"

#include <cstring>
#include <atomic>

namespace Configuration {
    Validator::Validator() {
        // Read fence for config. Shouldn't be necessary, but better safe than sorry.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void Validator::enterSection(const char* name, Configurable* value) {
        _path.push_back(name);  // For error handling

        try {
            value->validate();
        } catch (std::exception& ex) {
            log_config_error("Validation error at "; for (auto it : _path) { ss << '/' << it; } ss << ": " << ex.what());
        }

        value->group(*this);

        _path.erase(_path.begin() + (_path.size() - 1));
    }
}
