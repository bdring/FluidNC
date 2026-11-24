// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
  Native-build (macos/linux) stand-in for the real PWM drivers (esp32/PwmPin.cpp
  uses the ESP32 LEDC controller, rp2040/PwmPin.cpp its PWM slices). There's no
  real hardware to drive here, so this doesn't do anything electrically -- but
  every duty change is reported as an ordinary [MSG:PWM: ...] line so test
  scaffolding can observe it. Log-based rather than a dedicated buffered/polled
  interface: it reuses the same async-message channel (and, on the fixture_tests
  side, the same Controller.on_msg() dispatch) that any other [MSG:...] chatter
  already flows through, instead of adding a new report type or a new $ command.
  See fixture_tests/INTEGRATION_TEST_PLAN.md.

  Only real transitions are reported: PWM::set_output() (Spindles/PWMSpindle.cpp)
  already skips calling setDuty() at all when the value hasn't changed, so this
  doesn't need its own deduplication.
*/

#include "Driver/PwmPin.h"
#include "Config.h"
#include "Logging.h"

PwmPin::PwmPin(pinnum_t gpio, bool invert, uint32_t frequency) : _gpio(gpio), _frequency(frequency) {
    _period = 1000000 / frequency;
}

void PwmPin::setDuty(uint32_t duty) {
    // No timestamp here -- fixture_tests' Controller.on_msg() callback
    // records arrival time on the Python side instead, which is simpler
    // (no timing dependency needed in this file) and plenty precise for
    // checking the *shape* of a rate-adjusted power curve, which only
    // needs the sequence of values, not sub-millisecond firmware-side
    // timing.
    log_msg("PWM: gpio." << _gpio << "," << duty);
}

PwmPin::~PwmPin() {}
