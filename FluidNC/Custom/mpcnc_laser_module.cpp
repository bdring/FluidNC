/*
	mpcnc_laser_module.cpp
	Part of Grbl_ESP32

    This forces the 5V level shift enable on all the time.
    To deal with laser modules that can fire with no signal.

	copyright (c) 2020 -	Bart Dring. This file was intended for use on the ESP32

	Grbl_ESP32 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	Grbl_ESP32 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_MACHINE_INIT
/*
machine_init() is called when Grbl_ESP32 first starts. You can use it to do any
special things your machine needs at startup.
*/
void machine_init() {
    Pin levelShift = Pin::create(LVL_SHIFT_ENABLE);

    // force this on all the time
    log_info("Custom machine_init() Level Shift Enabled");
    levelShift.setAttr(Pin::Attr::Output);
    levelShift.on();
}
#endif
