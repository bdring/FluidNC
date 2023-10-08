// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

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

        const char* _start;
        const char* _end;

        const char* _key;
        const char* _keyend;
        const char* _value;
        const char* _valueend;

        PinOption(const char* start, const char* end);

        void tokenize();

    public:
        inline const char* operator()() const { return _key; }
        bool               is(const char* option) const;

        int    iValue() const;
        double dValue() const;

        const char* value() const;

        // Iterator support:
        inline PinOption const* operator->() const { return this; }
        inline PinOption        operator*() const { return *this; }
        PinOption&              operator++();

        bool operator==(const PinOption& o) const { return _key == o._key && _keyend == o._keyend; }
        bool operator!=(const PinOption& o) const { return _key != o._key || _keyend != o._keyend; }
    };

    // This parses the options passed to the Pin class.
    class PinOptionsParser {
        const char* _buffer;
        const char* _bufferEnd;

    public:
        PinOptionsParser(const char* buffer, const char* endBuffer);

        inline PinOption begin() const { return PinOption(_buffer, _bufferEnd); }
        inline PinOption end() const { return PinOption(_bufferEnd, _bufferEnd); }
    };
}
