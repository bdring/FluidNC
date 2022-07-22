#pragma once

#include "Arduino.h"
#include "../../FluidNC/src/Assert.h"
#include <random>
#include "esp32-hal-gpio.h"

struct SoftwarePin {
    SoftwarePin() : callback(), argument(nullptr), mode(0), driverValue(false), padValue(false), pinMode(0) {}

    void (*callback)(void*);
    void* argument;
    int   mode;

    bool driverValue;
    bool padValue;
    int  pinMode;

    void handleISR() { callback(argument); }

    void reset() {
        callback    = nullptr;
        argument    = nullptr;
        mode        = 0;
        driverValue = false;
        padValue    = false;
        pinMode     = 0;
    }

    void handlePadChangeWithHystesis(bool newval) {
        auto oldval = padValue;
        if (oldval != newval) {
            std::default_random_engine       generator;
            std::normal_distribution<double> distribution(5, 2);
            int                              count = int(distribution(generator));

            // Bound it a bit
            if (count < 0) {
                count = 0;
            } else if (count > 8) {
                count = 8;
            }
            count = count * 2 + 1;  // make it odd.

            auto currentVal = oldval;
            for (int i = 0; i < count; ++i) {
                currentVal = !currentVal;
                handlePadChange(currentVal);
            }
        }
    }

    void handlePadChange(bool newval) {
        auto oldval = padValue;
        if (oldval != newval) {
            switch (mode) {
                case RISING:
                    if (!oldval && newval) {
                        handleISR();
                    }
                    break;
                case FALLING:
                    if (oldval && !newval) {
                        handleISR();
                    }
                    break;
                case CHANGE:
                    if (oldval != newval) {
                        handleISR();
                    }
                    break;
            }
            padValue = newval;
        }
    }
};

typedef void (*HandleCircuit)(SoftwarePin* pins, int pin, bool value);

class SoftwareGPIO {
    SoftwareGPIO() {}
    SoftwareGPIO(const SoftwareGPIO&) = delete;

    SoftwarePin   pins[256];
    HandleCircuit virtualCircuit         = nullptr;
    bool          circuitHandlesHystesis = false;

public:
    static SoftwareGPIO& instance() {
        static SoftwareGPIO instance_;
        return instance_;
    }

    static void reset(HandleCircuit circuit, bool circuitHandlesHystesis) {
        auto& inst = instance();
        for (int i = 0; i < 256; ++i) {
            inst.pins[i].reset();
        }

        inst.virtualCircuit         = circuit;
        inst.circuitHandlesHystesis = circuitHandlesHystesis;
    }

    void setMode(int index, int mode) {
        auto& pin              = pins[index];
        auto  oldModeHasOutput = (pin.pinMode & OUTPUT) == OUTPUT;
        pin.pinMode            = mode;
        auto modeHasOutput     = (pin.pinMode & OUTPUT) == OUTPUT;

        if (modeHasOutput && !oldModeHasOutput) {
            if (virtualCircuit != nullptr) {
                virtualCircuit(pins, index, pin.driverValue);
            } else if (circuitHandlesHystesis) {
                pins[index].handlePadChange(pin.driverValue);
            }
        }
    }

    void writeOutput(int index, bool value) {
        auto oldValue = pins[index].padValue;

        if ((pins[index].pinMode & OUTPUT) == OUTPUT) {
            if (virtualCircuit != nullptr) {
                virtualCircuit(pins, index, value);
            } else if (circuitHandlesHystesis) {
                pins[index].handlePadChange(value);
            } else {
                pins[index].handlePadChangeWithHystesis(value);
            }
        } else {
            pins[index].handlePadChange(value);
        }
    }

    bool read(int index) const { return pins[index].padValue; }

    void attachISR(int index, void (*callback)(void* arg), void* arg, int mode) {
        auto& pin = pins[index];
        Assert(pin.mode == 0, "ISR mode should be 0 when attaching interrupt. Another interrupt is already attached.");

        pin.callback = callback;
        pin.argument = arg;
        pin.mode     = mode;
    }

    void detachISR(int index) {
        auto& pin    = pins[index];
        pin.mode     = 0;
        pin.argument = nullptr;
    }
};
