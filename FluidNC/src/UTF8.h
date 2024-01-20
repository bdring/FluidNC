// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vector>

class UTF8 {
private:
    uint32_t _num;
    int      _state = 0;

public:
    // Byte-at-a-time decoder.  Returns -1 for error, 1 for okay, 0 for keep trying
    int decode(uint8_t ch, uint32_t& value);

    // Vector-of-bytes decoder.  Returns true if the vector contains
    // a well-formed UTF8 sequence.
    bool decode(const std::vector<uint8_t>& in, uint32_t& value);

    // Encode to vector
    std::vector<uint8_t> encode(const uint32_t value);
};

void test_UTF8();
