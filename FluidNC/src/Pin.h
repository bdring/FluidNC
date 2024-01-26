// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Pins/PinDetail.h"
#include "Pins/PinCapabilities.h"
#include "Pins/PinAttributes.h"
#include "src/Machine/EventPin.h"

#include <esp_attr.h>  // IRAM_ATTR
#include <cstdint>
#include <string>
#include <cstring>
#include <utility>
#include <string_view>
#include "Assert.h"

// #define DEBUG_PIN_DUMP  // Pin debugging. WILL spam you with a lot of data!

// Pin class. A pin is basically a thing that can 'output', 'input' or do both. GPIO on an ESP32 comes to mind,
// but there are way more possible pins. Think about I2S/I2C/SPI extenders, RS485 driven pin devices and even
// WiFi wall sockets.
//
// Normally people define a pin using the yaml configuration, and specify there how a pin should behave. For
// example, ':low' is normally supported, which means 'default low'. For output pins this is relevant, because
// it basically means 'off is a high (3.3V) signal, on is a low (GND) signal'. By doing it like this, there is
// no longer a need for invert flags, and things like that.
//
// Pins are supposed to be fields during the lifetime of the MachineConfig. In normal operations, the pin is
// created by the parser (using the 'create' methods), then kept in fields from the configurable, and eventually
// cleaned up by the destructor. Pins cannot be copied, there always has to be 1 owner of a pin, and they shouldn't
// be thrown around in the application.
//
// Pins internally use PinDetail classes. PinDetail's are just implementation details for a certain type of pin.
// PinDetail is not exposed to developers, because they should never be used directly. Pin class is your
// one-stop-go-to-shop for an pin.
class Pin {
    // Helper for handling callbacks and mapping them to the proper class:
    //
    // Take note: this is placing the code in FLASH instead of IRAM, like it should. Use the #define's above
    // until this is fixed!

    // template <typename ThisType, void (ThisType::*Callback)()>
    // struct InterruptCallbackHelper {
    //     static void IRAM_ATTR callback(void* ptr) { (static_cast<ThisType*>(ptr)->*Callback)(); }
    // };

    // Helper for handling callbacks and mapping them to the proper class. This one is just meant
    // for backward compatibility:
    // template <void (*Callback)()>
    // struct InterruptCallbackHelper2 {
    //     static void IRAM_ATTR callback(void* /*ptr*/) { Callback(); }
    // };

    // The undefined pin and error pin are two special pins. Error pins always throw an error when they are used.
    // These are useful for unit testing, and for initializing pins that _always_ have to be defined by a user
    // (or else). Undefined pins are basically pins with no functionality. They don't have to be defined, but also
    // have no functionality when they are used.
    static Pins::PinDetail* undefinedPin;
    static Pins::PinDetail* errorPin;

    // Implementation details of this pin.
    Pins::PinDetail* _detail;

    static const char* parse(std::string_view str, Pins::PinDetail*& detail);

    inline Pin(Pins::PinDetail* detail) : _detail(detail) {}

public:
    using Capabilities = Pins::PinCapabilities;
    using Attr         = Pins::PinAttributes;

    // A default pin is an undefined pin.
    inline Pin() : _detail(undefinedPin) {}

    static const bool On  = true;
    static const bool Off = false;

    static const int NO_INTERRUPT = 0;
    static const int RISING_EDGE  = 1;
    static const int FALLING_EDGE = 2;
    static const int EITHER_EDGE  = 3;

    static const int ASSERTING   = 0x10;
    static const int DEASSERTING = 0x11;

    static Pin  create(std::string_view str);
    static bool validate(const char* str);

    // We delete the copy constructor, and implement the move constructor. The move constructor is required to support
    // the correct execution of 'return' in f.ex. `create` calls. It basically transfers ownership from the callee to the
    // caller of the Pin.
    inline Pin(const Pin& o) = delete;
    inline Pin(Pin&& o) : _detail(nullptr) { std::swap(_detail, o._detail); }

    inline Pin& operator=(const Pin& o) = delete;
    inline Pin& operator                =(Pin&& o) {
        std::swap(_detail, o._detail);
        return *this;
    }

    // Some convenience operators and functions:
    inline bool operator==(const Pin& o) const { return _detail == o._detail; }
    inline bool operator!=(const Pin& o) const { return _detail != o._detail; }

    inline bool undefined() const { return _detail == undefinedPin; }
    inline bool defined() const { return !undefined(); }

    // External libraries normally use digitalWrite, digitalRead and setMode. Since we cannot handle that behavior, we
    // just give back the pinnum_t for getNative.
    inline pinnum_t getNative(Capabilities expectedBehavior) const {
        Assert(_detail->capabilities().has(expectedBehavior), "Requested pin %s does not have the expected behavior.", name().c_str());
        return _detail->_index;
    }

    void write(bool value) const;
    void synchronousWrite(bool value) const;

    inline bool read() const { return _detail->read() != 0; }

    inline void setAttr(Attr attributes) const { _detail->setAttr(attributes); }

    inline Attr getAttr() const { return _detail->getAttr(); }

    inline void on() const { write(1); }
    inline void off() const { write(0); }

    static Pin Error() { return Pin(errorPin); }

    void registerEvent(EventPin* obj) { _detail->registerEvent(obj); };

    // Other functions:
    Capabilities capabilities() const { return _detail->capabilities(); }

    inline std::string name() const { return _detail->toString(); }

    void report(const char* legend);
    void report(std::string legend) { report(legend.c_str()); }

    inline void swap(Pin& o) { std::swap(o._detail, _detail); }

    ~Pin();
};
