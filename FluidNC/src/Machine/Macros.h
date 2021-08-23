#pragma once

/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn, Mitch Bradley

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

#include "../Configuration/Configurable.h"
#include "../WebUI/InputBuffer.h"  // WebUI::inputBuffer

namespace Machine {
    class Macros : public Configuration::Configurable {
    public:
        static const int n_startup_lines = 2;
        static const int n_macros        = 4;

    private:
        String _startup_line[n_startup_lines];
        String _macro[n_macros];

    public:
        Macros() = default;

        void run_macro(size_t index) {
            if (index >= n_macros) {
                return;
            }
            String macro = _macro[index];
            if (macro == "") {
                return;
            }

            // & is a proxy for newlines in macros, because you cannot
            // enter a newline directly in a config file string value.
            macro.replace('&', '\n');
            macro += "\n";

            WebUI::inputBuffer.push(macro.c_str());
        }

        String startup_line(size_t index) {
            if (index >= n_startup_lines) {
                return "";
            }
            String s = _startup_line[index];
            if (s == "") {
                return s;
            }
            // & is a proxy for newlines in startup lines, because you cannot
            // enter a newline directly in a config file string value.
            s.replace('&', '\n');
            return s + "\n";
        }

        // Configuration helpers:

        // TODO: We could validate the startup lines

        void group(Configuration::HandlerBase& handler) override {
            handler.item("n0", _startup_line[0]);
            handler.item("n1", _startup_line[1]);
            handler.item("macro0", _macro[0]);
            handler.item("macro1", _macro[1]);
            handler.item("macro2", _macro[2]);
            handler.item("macro3", _macro[3]);
        }

        ~Macros() {}
    };
}
