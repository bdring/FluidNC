// Copyright (c) 2021 -	Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "AfterParse.h"

#include "Configurable.h"
#include "../System.h"
#include "../Logging.h"

#include <cstring>

namespace Configuration {
    void AfterParse::enterSection(const char* name, Configurable* value) {
        _path.push_back(name);  // For error handling

        try {
            value->afterParse();
        } catch (const AssertionFailed& ex) {
            // Log something meaningful to the user:
            log_error("Initialization error at "; for (auto it : _path) { ss << '/' << it; } ss << ": " << ex.msg);

            // Set the state to config alarm, so users can't run time machine.
            sys.state = State::ConfigAlarm;
        }

        value->group(*this);

        _path.erase(_path.begin() + (_path.size() - 1));
    }
}
