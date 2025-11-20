// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <string_view>
#include <cstdint>

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

        std::string_view _options;
        std::string_view _option;
        std::string_view _key;
        std::string_view _value;

        PinOption(const std::string_view options);

        void tokenize();

    public:
        bool is(const char* option) const;

        int32_t iValue() const;

        inline const std::string_view operator()() { return _option; }
        inline const std::string_view value() { return _value; }
        inline const std::string_view key() { return _key; }

        // Iterator support:
        inline PinOption const* operator->() const { return this; }
        inline PinOption        operator*() const { return *this; }
        PinOption&              operator++();

        bool operator==(const PinOption& o) const { return _key == o._key; }
        bool operator!=(const PinOption& o) const { return _key != o._key; }
    };

    // This parses the options passed to the Pin class.
    class PinOptionsParser {
        std::string_view _options;

    public:
        PinOptionsParser(std::string_view options);

        inline PinOption begin() const { return PinOption(_options); }
        inline PinOption end() const { return PinOption(std::string_view()); }
    };
}
