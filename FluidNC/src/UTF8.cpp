// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#define PASS_THROUGH_80_BF
// #define TEST_UTF8

#include "UTF8.h"

// Returns 1 if we have a valid sequence, and value is set
// Returns 0 if we are in the middle of a sequence
// Returns -1 if there is a sequence error
int UTF8::decode(uint8_t ch, uint32_t& value) {
    if (_state) {
        if ((ch & 0xc0) != 0x80) {
            // Trailing bytes in a sequence must have 10 in the two high bits
            _state = 0;
            return -1;
        }
        // Otherwise, ch is between 0x80 and 0xbf, so it is the
        // second, third, or fourth byte of a UTF8 sequence
        _state--;
        _num = (_num << 6) | (ch & 0x3f);
        if (_state) {
            return 0;
        }
        value = _num;
        return 1;
    }
    // After this point, _state is zero
    if (ch < 0x80) {
        // 1-byte sequence - No decoding necessary
        value = ch;
        return 1;
    }
#ifdef PASS_THROUGH_80_BF
    if (ch < 0xbf) {
        // UTF8 uses 0x80-0xbf only for continuation bytes, i.e. the second,
        // third, or fourth byte of a sequence.  Therefore, a byte in that
        // range should be an error if it occurs outside of a sequence.
        // But GRBL uses that range for realtime characters, and all
        // preexisting GRBL serial senders send such bytes unencoded.
        // By passing them through without an error, we can be backwards
        // compatible.
        value = ch;
        return 1;
    }
#endif

    if (ch >= 0xf8) {
        // Invalid start byte
        return -1;
    }
    if (ch >= 0xf0) {
        _state = 3;  // Start of 4-byte sequence
        _num   = ch & 0x07;
        return 0;
    }
    if (ch >= 0xe0) {
        _state = 2;  // Start of 3-byte sequence
        _num   = ch & 0x0f;
        return 0;
    }
    if (ch >= 0xc0) {
        _state = 1;  // Start of 2-byte sequence
        _num   = ch & 0x1f;
        return 0;
    }
    // Otherwise we received a continuation byte with the two high bits == 10,
    // i.e. ch between 0x80 and 0xbf, when not in the midst of a sequence
    return -1;
}
bool UTF8::decode(const std::vector<uint8_t>& input, uint32_t& value) {
    int len = input.size();
    for (auto& ch : input) {
        --len;
        int result = decode(ch, value);
        if (result == -1) {
            return false;
        }
        if (result == 1) {
            return len == 0;  // Error if there are more bytes in the input
        }
    }
    // Reached end of input without finishing the decode
    return false;
}
std::vector<uint8_t> UTF8::encode(const uint32_t value) {
    std::vector<uint8_t> output;
    if (value >= 0x110000) {
        // In this case, the returned vector will be empty
        return output;
    }
    if (value >= 0x100000) {
        output.push_back(0xf0 | ((value >> 18) & 0x07));
        output.push_back(0x80 | ((value >> 12) & 0x3f));
        output.push_back(0x80 | ((value >> 6) & 0x3f));
        output.push_back(0x80 | (value & 0x3f));
        return output;
    }
    if (value >= 0x800) {
        output.push_back(0xe0 | ((value >> 12) & 0x0f));
        output.push_back(0x80 | ((value >> 6) & 0x3f));
        output.push_back(0x80 | (value & 0x3f));
        return output;
    }
    if (value >= 0x80) {
        output.push_back(0xc0 | ((value >> 6) & 0x01f));
        output.push_back(0x80 | (value & 0x3f));
        return output;
    }
    output.push_back(value);
    return output;
}

#ifdef TEST_UTF8
#    include <cstdio>

static bool decode_test(UTF8* utf8, std::vector<uint8_t> input, uint32_t& value) {
    for (auto& ch : input) {
        printf("%x ", ch);
    }
    printf("-> ");
    auto res = utf8->decode(input, value);
    if (res) {
        printf("%x\n", value);
    } else {
        printf("ERROR\n");
    }
    return res;
}

static bool encode_test(UTF8* utf8, uint32_t value) {
    auto encoded = utf8->encode(value);
    printf("%x -> ", value);
    if (encoded.size()) {
        uint32_t out_value;
        bool     res = decode_test(utf8, encoded, out_value);
        if (!res) {
            return false;
        }
        if (out_value != value) {
            printf(" -- Incorrect value\n");
            return false;
        }
        return true;
    } else {
        printf("ERROR\n");
        return false;
    }
}

void test_UTF8() {
    UTF8* utf8 = new UTF8();
    encode_test(utf8, 0x7f);
    encode_test(utf8, 0x80);
    encode_test(utf8, 0x90);
    encode_test(utf8, 0xa0);
    encode_test(utf8, 0xbf);
    encode_test(utf8, 0x100);
    encode_test(utf8, 0x13f);
    encode_test(utf8, 0x140);
    encode_test(utf8, 0x17f);
    encode_test(utf8, 0x1ff);
    encode_test(utf8, 0x200);
    encode_test(utf8, 0x2ff);
    encode_test(utf8, 0x7ff);
    encode_test(utf8, 0x800);
    encode_test(utf8, 0xffff);
    encode_test(utf8, 0x100000);
    encode_test(utf8, 0x10ffff);
    encode_test(utf8, 0x110000);
    uint32_t out_value;
    decode_test(utf8, std::vector<uint8_t> { 0x80 }, out_value);              // continuation byte outside
    decode_test(utf8, std::vector<uint8_t> { 0xc0 }, out_value);              // Incomplete sequence
    decode_test(utf8, std::vector<uint8_t> { 0xc0, 0x30 }, out_value);        // Non-continuation inside
    decode_test(utf8, std::vector<uint8_t> { 0xc0, 0x80, 0x30 }, out_value);  // Extra bytes
    delete utf8;
}
#else
void test_UTF8() {}
#endif
