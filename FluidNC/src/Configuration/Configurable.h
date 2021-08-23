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

#pragma once

#include "Generator.h"
#include "Parser.h"

namespace Configuration {
    class HandlerBase;

    class Configurable {
        Configurable(const Configurable&) = delete;
        Configurable(Configurable&&)      = default;

        Configurable& operator=(const Configurable&) = delete;
        Configurable& operator=(Configurable&&) = default;

    public:
        Configurable() = default;

        virtual void validate() const {};
        virtual void group(HandlerBase& handler) = 0;
        virtual void afterParse() {}
        // virtual const char* name() const = 0;

        virtual ~Configurable() {}
    };
}
