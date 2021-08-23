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

#include "PinDetail.h"

#include "../Assert.h"

namespace Pins {
    void PinDetail::attachInterrupt(void (*callback)(void*), void* arg, int mode) {
        Assert(false, "Interrupts are not supported by pin %d", _index);
    }
    void PinDetail::detachInterrupt() {
        Assert(false, "Interrupts are not supported by pin %d", _index);
        ;
    }
}
