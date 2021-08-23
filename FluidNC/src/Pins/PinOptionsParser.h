/*
    Part of Grbl_ESP32
    2021 -  Stefan de Bruijn

    Grbl_ESP32 is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Grbl_ESP32 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Grbl_ESP32.  If not, see <http://www.gnu.org/licenses/>.
*/

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
