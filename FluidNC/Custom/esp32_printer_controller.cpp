// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Custom code for esp32_printer_controller

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
