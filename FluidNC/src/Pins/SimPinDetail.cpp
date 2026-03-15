// Copyright (c) 2025 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "SimPinDetail.h"
#include "PinOptionsParser.h"
#include "../Stepping.h"

// A "sim" pin is a step or direction pin used with simulator_engine.cpp,
// the interface to a web-based machine motion visualizer.
// sim.0 refers to the visualizer X axis
// sim.1 refers to the visualizer Y axis
// sim.2 refers to the visualizer Z axis
// The same sim pin can be used for both step_pin and dir_pin
// Options: "low" (active low), "high" (active high, default)

namespace Pins {
    SimPinDetail::SimPinDetail(pinnum_t axis, PinOptionsParser options) :
        PinDetail(axis), _axis(axis), _attributes(PinAttributes::Undefined) {
        _name = "sim:" + std::to_string(axis);

        // Parse options for active level
        for (auto opt : options) {
            if (opt.is("low")) {
                _attributes = _attributes | PinAttributes::ActiveLow;
                _name += ":low";
            } else if (opt.is("high")) {
                // Default: Active HIGH.
            }
        }

        // Always set Output attribute after parsing options, matching GPIOPinDetail pattern
        _attributes = _attributes | PinAttributes::Output;

        _inverted = _attributes.has(PinAttributes::ActiveLow);
    }

    PinCapabilities SimPinDetail::capabilities() const {
        return PinCapabilities::Output | PinCapabilities::Native;
    }

    void SimPinDetail::write(bool high) {
        // Apply inversion if the pin is active low
        bool value = _inverted ^ (bool)high;
        // Note: actual stepping is handled by the step engine
        // This method exists for API compatibility
    }

    bool SimPinDetail::read() {
        // Simulator pins don't have readable state
        return false;
    }

    void SimPinDetail::setAttr(PinAttributes value, uint32_t frequency) {
        // Accumulate attributes
        _attributes = _attributes | value;
    }

    PinAttributes SimPinDetail::getAttr() const {
        return _attributes;
    }
}
