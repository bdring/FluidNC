/*
	esp32_printer_controller.cpp (copy and use your machine name)
	Part of Grbl_ESP32

	copyright (c) 2020 -	Bart Dring. This file was intended for use on the ESP32

	CPU. Do not use this with Grbl for atMega328P

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

#define STEPPERS_DISABLE_PIN_X 138
#define STEPPERS_DISABLE_PIN_Y 134
#define STEPPERS_DISABLE_PIN_Z 131
#define STEPPERS_DISABLE_PIN_A 139

#define FAN1_PIN 13
#define FAN2_PIN 142
#define FAN3_PIN 143

#define BED_PIN 4
#define NOZZLE_PIN 2

void machine_init() {
    // Enable steppers
    digitalWrite(STEPPERS_DISABLE_PIN_X, LOW);  // enable
    digitalWrite(STEPPERS_DISABLE_PIN_Y, LOW);  // enable
    digitalWrite(STEPPERS_DISABLE_PIN_Z, LOW);  // enable
    digitalWrite(STEPPERS_DISABLE_PIN_A, LOW);  // enable

    // digitalWrite(FAN1_PIN, LOW); // comment out for JTAG debugging

    digitalWrite(FAN2_PIN, LOW);  // disable
    digitalWrite(FAN3_PIN, LOW);  // disable

    digitalWrite(BED_PIN, LOW);     // disable
    digitalWrite(NOZZLE_PIN, LOW);  // disable
}
