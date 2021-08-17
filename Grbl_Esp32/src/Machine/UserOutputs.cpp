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

#include "UserOutputs.h"

namespace Machine {
    UserOutputs::UserOutputs() {
        for (int i = 0; i < 4; ++i) {
            _analogFrequency[i] = 5000;
        }
    }

    void UserOutputs::init() {
        // Setup M62,M63,M64,M65 pins
        for (int i = 0; i < 4; ++i) {
            myDigitalOutputs[i] = new UserOutput::DigitalOutput(i, _digitalOutput[i]);
            myAnalogOutputs[i]  = new UserOutput::AnalogOutput(i, _analogOutput[i], _analogFrequency[i]);
        }
    }

    void UserOutputs::all_off() {
        for (size_t io_num = 0; io_num < MaxUserDigitalPin; io_num++) {
            myDigitalOutputs[io_num]->set_level(false);
            myAnalogOutputs[io_num]->set_level(0);
        }
    }

    bool UserOutputs::setDigital(size_t io_num, bool isOn) { return myDigitalOutputs[io_num]->set_level(isOn); }
    bool UserOutputs::setAnalogPercent(size_t io_num, float percent) {
        auto     analog    = myAnalogOutputs[io_num];
        uint32_t numerator = uint32_t(percent / 100.0f * analog->denominator());
        return analog->set_level(numerator);
    }

    void UserOutputs::group(Configuration::HandlerBase& handler) {
        handler.item("analog0", _analogOutput[0]);
        handler.item("analog1", _analogOutput[1]);
        handler.item("analog2", _analogOutput[2]);
        handler.item("analog3", _analogOutput[3]);
        handler.item("analog_frequency0", _analogFrequency[0]);
        handler.item("analog_frequency1", _analogFrequency[1]);
        handler.item("analog_frequency2", _analogFrequency[2]);
        handler.item("analog_frequency3", _analogFrequency[3]);
        handler.item("digital0", _digitalOutput[0]);
        handler.item("digital1", _digitalOutput[1]);
        handler.item("digital2", _digitalOutput[2]);
        handler.item("digital3", _digitalOutput[3]);
    }
}
