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

#include <esp_attr.h>  // IRAM_ATTR
#include "VoidPinDetail.h"

namespace Pins {
    VoidPinDetail::VoidPinDetail(int number) : PinDetail(number) {}
    VoidPinDetail::VoidPinDetail(const PinOptionsParser& options) : VoidPinDetail() {}

    PinCapabilities VoidPinDetail::capabilities() const {
        // Void pins support basic functionality. It just won't do you any good.
        return PinCapabilities::Output | PinCapabilities::Input | PinCapabilities::ISR | PinCapabilities::Void;
    }

    void IRAM_ATTR VoidPinDetail::write(int high) {}
    int            VoidPinDetail::read() { return 0; }
    void           VoidPinDetail::setAttr(PinAttributes value) {}
    PinAttributes  VoidPinDetail::getAttr() const { return PinAttributes::None; }

    String VoidPinDetail::toString() { return "NO_PIN"; }

}
