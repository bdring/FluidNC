// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

class Pin;  // Forward declaration

namespace Pins {
    // Pin options are passed as PinOption object. This is a simple C++ forward iterator,
    // which will implicitly convert pin options to lower case, so you can simply do
    // stuff like this:
    //
    // for (auto it : options) {
    //   const char* currentOption = it();
    //   ...
    //   if (currentOption.is("pu")) { /* configure pull up */ }
    //   ...
    // }
    //
    // This is a very light-weight parser for pin options, configured as 'pu:high:etc'
    // (full syntax f.ex.: gpio.12:pu:high)

    class PinOptionsParser;

    class PinOption {
        friend class PinOptionsParser;

        char*       _start;
        const char* _end;

        const char* _key;
        const char* _value;

        PinOption(char* start, const char* end);

        void tokenize();

    public:
        inline const char* operator()() const { return _key; }
        bool               is(const char* option) const;

        int                iValue() const;
        double             dValue() const;
        inline const char* value() const { return _value; }

        // Iterator support:
        inline PinOption const* operator->() const { return this; }
        inline PinOption        operator*() const { return *this; }
        PinOption&              operator++();

        bool operator==(const PinOption& o) const { return _key == o._key; }
        bool operator!=(const PinOption& o) const { return _key != o._key; }
    };

    // Options parser. This basically parses the options passed to the Pin class. Destroys
    // the original options, and passes the options as lower case items to the enumerator.
    // For the lazy people that want safe, reliable, easy parsing :-)
    class PinOptionsParser {
        char* _buffer;
        char* _bufferEnd;

    public:
        PinOptionsParser(char* buffer, char* endBuffer);

        inline PinOption begin() const { return PinOption(_buffer, _bufferEnd); }
        inline PinOption end() const { return PinOption(_bufferEnd, _bufferEnd); }
    };
}
