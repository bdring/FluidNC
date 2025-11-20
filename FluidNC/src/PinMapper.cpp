#include "Config.h"
#include "PinMapper.h"
#include "Pins/GPIOPinDetail.h"
#include "Driver/fluidnc_gpio.h"

// Pin mapping lets you use non-GPIO pins as though they were GPIOs by
// storing Pin object references in an array indexed by a small
// integer.  An offset is added to the index to push the number beyond
// the range of real GPIO numbers, while staying within the numeric
// range of a pinnum_t.  That index+offset can be treated as a
// pinnum_t and passed to library routines whose API requires a GPIO
// number.  This lets you use I2S pins for chip selects and possibly
// for other purposes.  It works for libraries that use only
// pinMode(), digitalWrite() and digitalRead(), but fails for
// libraries that demand real GPIO pins, perhaps by modifying the IO
// Matrix.  For such cases, the real GPIO number - lower than the
// offset - can be used directly.  When the pinMode(), digitalWrite()
// and digitalRead() overloads encounter a real GPIO number, they pass
// the operation through to the lower level gpio_mode(),
// gpio_read() and gpio_write() routines.

#ifndef OPEN_DRAIN
// Bit mask values compatible with Arduino pinMode()
#    define INPUT 0x01
#    define OUTPUT 0x03
#    define PULLUP 0x04
#    define PULLDOWN 0x08
#    define OPEN_DRAIN 0x10
#endif

namespace {
    class PinMap {
    public:
        static const int BOUNDARY = MAX_N_GPIO;

    private:
        static const int N_PIN_MAPPINGS = 127 - BOUNDARY;

    public:
        Pin* _mapping[N_PIN_MAPPINGS];

        PinMap() {
            for (pinnum_t i = 0; i < N_PIN_MAPPINGS; ++i) {
                _mapping[i] = nullptr;
            }
        }

        pinnum_t Claim(Pin* pin) {
            for (pinnum_t i = 0; i < N_PIN_MAPPINGS; ++i) {
                if (_mapping[i] == nullptr) {
                    _mapping[i] = pin;
                    return i + BOUNDARY;
                }
            }
            return 0;
        }

        void Release(pinnum_t idx) { _mapping[idx - BOUNDARY] = nullptr; }

        static PinMap& instance() {
            static PinMap instance;
            return instance;
        }
    };
}

// See header file for more information.

PinMapper::PinMapper() : _mappedId(0) {}

PinMapper::PinMapper(Pin& pin) {
    _mappedId = PinMap::instance().Claim(&pin);

    Assert(_mappedId != 0, "Cannot claim pin. Too many mapped pins are used.");
}

// To aid return values and assignment
PinMapper::PinMapper(PinMapper&& o) : _mappedId(0) {
    std::swap(_mappedId, o._mappedId);
}

PinMapper& PinMapper::operator=(PinMapper&& o) {
    // Special case for `a=a`. If we release there, things go wrong.
    if (&o != this) {
        if (_mappedId != 0) {
            PinMap::instance().Release(_mappedId);
            _mappedId = 0;
        }
        std::swap(_mappedId, o._mappedId);
    }
    return *this;
}

// Clean up when we get destructed.
PinMapper::~PinMapper() {
    if (_mappedId != 0) {
        PinMap::instance().Release(_mappedId);
    }
}

// Arduino compatibility function which uses a mapped pin ID.  We need this
// in order to use I2SO pins as CS pins for the TMCStepper library.

// The first argument must be uint8_t to match the signature of the Arduino library,
// otherwise this will not override the weak definition in the library.
void IRAM_ATTR digitalWrite(uint8_t upin, uint8_t val) {
    pinnum_t pin = upin;

    if (pin < PinMap::BOUNDARY) {
        gpio_write(pin, val);
        return;
    }
    const Pin* thePin = PinMap::instance()._mapping[pin - PinMap::BOUNDARY];
    if (thePin) {
        thePin->synchronousWrite(val);
    }
}

void IRAM_ATTR pinMode(pinnum_t pin, uint8_t mode) {
    if (pin < PinMap::PinMap::BOUNDARY) {
        gpio_mode(pin, mode & INPUT, mode & OUTPUT, mode & PULLUP, mode & PULLDOWN, mode & OPEN_DRAIN);
        return;
    }

    const Pin* thePin = PinMap::instance()._mapping[pin - PinMap::BOUNDARY];
    if (!thePin) {
        return;
    }

    Pins::PinAttributes attr = Pins::PinAttributes::None;
    if ((mode & OUTPUT) == OUTPUT) {
        attr = attr | Pins::PinAttributes::Output;
    }
    if ((mode & INPUT) == INPUT) {
        attr = attr | Pins::PinAttributes::Input;
    }
    if ((mode & PULLUP) == PULLUP) {
        attr = attr | Pins::PinAttributes::PullUp;
    }
    if ((mode & PULLDOWN) == PULLDOWN) {
        attr = attr | Pins::PinAttributes::PullDown;
    }

    thePin->setAttr(attr);
}

int IRAM_ATTR digitalRead(pinnum_t pin) {
    if (pin < PinMap::BOUNDARY) {
        return gpio_read(pin);
    }
    const Pin* thePin = PinMap::instance()._mapping[pin - PinMap::BOUNDARY];
    return (thePin) ? thePin->read() : 0;
}
