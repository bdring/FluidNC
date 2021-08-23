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
