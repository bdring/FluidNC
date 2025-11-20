#pragma once

// Pin mapper is a class that maps 'Pin' objects to Arduino digitalWrite / digitalRead / setMode. This
// can be useful for support of external libraries, while keeping all the things that Pin has to offer.
//
// It's designed to be easy to use. Basically just: `PinMapper myMap(pinToMap);`. It doesn't *own* the
// pin, it merely uses a pointer. Then, the external library can use `myMap.pinId()` as its pin number.
// Once the mapper goes out of scope (or is destructed if it's a field), the mapping is implicitly removed.
// Note that this is merely for external libraries that don't allow us to pass user data such as a void*...

#include "Pin.h"

class PinMapper {
    pinnum_t _mappedId;

public:
    // Default constructor. Doesn't map anything
    PinMapper();

    // Constructor that maps a pin to some Arduino pin ID
    explicit PinMapper(Pin& pin);

    // We do not want object copies
    PinMapper(const Pin& o)            = delete;
    PinMapper& operator=(const Pin& o) = delete;

    // For return values, we have to add some move semantics. This is just to
    // support trivial assignment cases and return values. Normally: don't use it.
    // All these constructors just pass along the id by swapping it. If a pinmapper
    // goes out of scope, it is destructed.
    PinMapper(PinMapper&& o);
    PinMapper& operator=(PinMapper&& o);

    // The ID of the pin, as used by digitalWrite, digitalRead and setMode.
    inline pinnum_t pinId() const { return _mappedId; }

    // Destructor. Implicitly removes the mapping.
    ~PinMapper();
};
