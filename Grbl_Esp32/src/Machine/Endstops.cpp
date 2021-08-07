#include "Endstops.h"

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

namespace Machine {
    Endstops::Endstops(int axis, int gang) : _axis(axis), _gang(gang) {
        _negLimitPin = new LimitPin(_negPin, _axis, _gang, -1, _hardLimits);
        _posLimitPin = new LimitPin(_posPin, _axis, _gang, 1, _hardLimits);
        _allLimitPin = new LimitPin(_allPin, _axis, _gang, 0, _hardLimits);
    }

    void Endstops::init() {
        _negLimitPin->init();
        _posLimitPin->init();
        _allLimitPin->init();
    }

    void Endstops::validate() const {
        //    if (_dual.defined()) {
        //        Assert(_positive.undefined(), "If dual endstops are defined, you cannot also define positive and negative endstops");
        //        Assert(_negative.undefined(), "If dual endstops are defined, you cannot also define positive and negative endstops");
        //    }
        //    if (_positive.defined() || _negative.defined()) {
        //        Assert(_positive.undefined(), "If positive or negative endstops are defined, you cannot also define dual endstops");
        //    }
    }

    void Endstops::group(Configuration::HandlerBase& handler) {
        handler.item("limit_neg", _negPin);
        handler.item("limit_pos", _posPin);
        handler.item("limit_all", _allPin);
        handler.item("hard_limits", _hardLimits);
    }
}
