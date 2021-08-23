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

#include "Generator.h"

#include "Configurable.h"

#include <cstring>
#include <cstdio>
#include <atomic>

namespace Configuration {
    Generator::Generator(SimpleOutputStream& dst, int indent) : indent_(indent), dst_(dst) {
        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
    }

    void Generator::enter(const char* name) {
        indent();
        dst_ << name << ":\n";
        indent_++;
    }

    void Generator::add(Configuration::Configurable* configurable) {
        if (configurable != nullptr) {
            configurable->group(*this);
        }
    }

    void Generator::leave() {
        if (!lastIsNewline_) {
            dst_ << '\n';
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
