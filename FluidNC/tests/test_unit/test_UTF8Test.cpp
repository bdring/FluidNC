// Test suite for UTF8 encoding/decoding
#include "gtest/gtest.h"
#include "UTF8.h"

namespace {

// ============================================================================
// UTF8::decode(uint8_t) - Byte-at-a-time decoder tests
// ============================================================================

TEST(UTF8, DecodeSingleByteASCII) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // ASCII 'A' (0x41)
    int8_t result = decoder.decode(0x41, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x41);
}

TEST(UTF8, DecodeSingleByteLow) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // ASCII '0' (0x30)
    int8_t result = decoder.decode(0x30, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x30);
}

TEST(UTF8, DecodeSingleByteHigh) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // ASCII DEL (0x7F)
    int8_t result = decoder.decode(0x7F, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x7F);
}

TEST(UTF8, DecodeTwoByteSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded © (U+00A9): 0xC2 0xA9
    int8_t result = decoder.decode(0xC2, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0xA9, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x00A9);
}

TEST(UTF8, DecodeThreeByteSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded € (U+20AC): 0xE2 0x82 0xAC
    int8_t result = decoder.decode(0xE2, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0x82, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0xAC, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x20AC);
}

TEST(UTF8, DecodeFourByteSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded emoji 😀 (U+1F600): 0xF0 0x9F 0x98 0x80
    int8_t result = decoder.decode(0xF0, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0x9F, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0x98, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0x80, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x1F600);
}

TEST(UTF8, DecodeInvalidStartByte) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // 0xF8 is invalid start byte
    int8_t result = decoder.decode(0xF8, value);
    EXPECT_EQ(result, -1);  // Error
}

TEST(UTF8, DecodeContinuationByteOutsideSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // 0xA9 is a continuation byte. With PASS_THROUGH_80_BF enabled in UTF8.cpp,
    // bytes in the range 0x80-0xBF are passed through without error for backwards compatibility
    int8_t result = decoder.decode(0xA9, value);
    EXPECT_EQ(result, 1);  // Returns success (not error) due to PASS_THROUGH_80_BF
    EXPECT_EQ(value, 0xA9);
}

TEST(UTF8, DecodeLatin1Supplement) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded ñ (U+00F1): 0xC3 0xB1
    int8_t result = decoder.decode(0xC3, value);
    EXPECT_EQ(result, 0);
    
    result = decoder.decode(0xB1, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0xF1);
}

TEST(UTF8, DecodeCyrillic) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded Ж (U+0416): 0xD0 0x96
    int8_t result = decoder.decode(0xD0, value);
    EXPECT_EQ(result, 0);
    
    result = decoder.decode(0x96, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x416);
}

TEST(UTF8, DecodeGreek) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded Ω (U+03A9): 0xCE 0xA9
    int8_t result = decoder.decode(0xCE, value);
    EXPECT_EQ(result, 0);
    
    result = decoder.decode(0xA9, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x3A9);
}

TEST(UTF8, DecodeChineseCharacter) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // UTF-8 encoded 中 (U+4E2D): 0xE4 0xB8 0xAD
    int8_t result = decoder.decode(0xE4, value);
    EXPECT_EQ(result, 0);
    
    result = decoder.decode(0xB8, value);
    EXPECT_EQ(result, 0);
    
    result = decoder.decode(0xAD, value);
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0x4E2D);
}

// ============================================================================
// UTF8::decode(vector) - Vector-of-bytes decoder tests
// ============================================================================

TEST(UTF8, DecodeVectorASCII) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0x41};  // 'A'
    
    bool result = decoder.decode(input, value);
    EXPECT_TRUE(result);
    EXPECT_EQ(value, 0x41);
}

TEST(UTF8, DecodeVectorTwoBytes) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0xC2, 0xA9};  // ©
    
    bool result = decoder.decode(input, value);
    EXPECT_TRUE(result);
    EXPECT_EQ(value, 0x00A9);
}

TEST(UTF8, DecodeVectorThreeBytes) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0xE2, 0x82, 0xAC};  // €
    
    bool result = decoder.decode(input, value);
    EXPECT_TRUE(result);
    EXPECT_EQ(value, 0x20AC);
}

TEST(UTF8, DecodeVectorFourBytes) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0xF0, 0x9F, 0x98, 0x80};  // 😀
    
    bool result = decoder.decode(input, value);
    EXPECT_TRUE(result);
    EXPECT_EQ(value, 0x1F600);
}

TEST(UTF8, DecodeVectorInvalidSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0xC2, 0x41};  // Invalid: second byte should be continuation
    
    bool result = decoder.decode(input, value);
    EXPECT_FALSE(result);
}

TEST(UTF8, DecodeVectorIncompleteSequence) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0xC2};  // Incomplete: missing second byte
    
    bool result = decoder.decode(input, value);
    EXPECT_FALSE(result);
}

TEST(UTF8, DecodeVectorExtraBytes) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {0x41, 0x42};  // Two complete sequences when expecting one
    
    bool result = decoder.decode(input, value);
    EXPECT_FALSE(result);
}

TEST(UTF8, DecodeVectorEmpty) {
    UTF8 decoder;
    uint32_t value = 0;
    std::vector<uint8_t> input = {};
    
    bool result = decoder.decode(input, value);
    EXPECT_FALSE(result);
}

// ============================================================================
// UTF8::encode - Encoding tests
// ============================================================================

TEST(UTF8, EncodeSingleByteASCII) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x41);  // 'A'
    
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0x41);
}

TEST(UTF8, EncodeTwoBytes) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0xA9);  // ©
    
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0xC2);
    EXPECT_EQ(result[1], 0xA9);
}

TEST(UTF8, EncodeThreeBytes) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x20AC);  // €
    
    EXPECT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 0xE2);
    EXPECT_EQ(result[1], 0x82);
    EXPECT_EQ(result[2], 0xAC);
}

TEST(UTF8, EncodeFourBytes) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x1F600);  // 😀
    
    EXPECT_EQ(result.size(), 4);
    EXPECT_EQ(result[0], 0xF0);
    EXPECT_EQ(result[1], 0x9F);
    EXPECT_EQ(result[2], 0x98);
    EXPECT_EQ(result[3], 0x80);
}

TEST(UTF8, EncodeInvalid) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x110000);  // Out of range
    
    EXPECT_EQ(result.size(), 0);  // Empty vector for invalid codepoint
}

TEST(UTF8, EncodeCyrillic) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x0416);  // Ж
    
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0xD0);
    EXPECT_EQ(result[1], 0x96);
}

TEST(UTF8, EncodeGreek) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x03A9);  // Ω
    
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0xCE);
    EXPECT_EQ(result[1], 0xA9);
}

// ============================================================================
// UTF8 Round-trip tests (encode then decode)
// ============================================================================

TEST(UTF8, RoundTripASCII) {
    UTF8 encoder, decoder;
    uint32_t original = 0x41;  // 'A'
    
    auto encoded = encoder.encode(original);
    uint32_t decoded = 0;
    decoder.decode(encoded, decoded);
    
    EXPECT_EQ(decoded, original);
}

TEST(UTF8, RoundTripTwoBytes) {
    UTF8 encoder, decoder;
    uint32_t original = 0xA9;  // ©
    
    auto encoded = encoder.encode(original);
    uint32_t decoded = 0;
    decoder.decode(encoded, decoded);
    
    EXPECT_EQ(decoded, original);
}

TEST(UTF8, RoundTripThreeBytes) {
    UTF8 encoder, decoder;
    uint32_t original = 0x20AC;  // €
    
    auto encoded = encoder.encode(original);
    uint32_t decoded = 0;
    decoder.decode(encoded, decoded);
    
    EXPECT_EQ(decoded, original);
}

TEST(UTF8, RoundTripFourBytes) {
    UTF8 encoder, decoder;
    uint32_t original = 0x1F600;  // 😀
    
    auto encoded = encoder.encode(original);
    uint32_t decoded = 0;
    decoder.decode(encoded, decoded);
    
    EXPECT_EQ(decoded, original);
}

TEST(UTF8, RoundTripMultipleCharacters) {
    UTF8 encoder, decoder;
    
    std::vector<uint32_t> originals = {0x41, 0xA9, 0x20AC, 0x1F600};
    
    for (auto original : originals) {
        auto encoded = encoder.encode(original);
        UTF8 fresh_decoder;  // Fresh decoder for each character
        uint32_t decoded = 0;
        bool success = fresh_decoder.decode(encoded, decoded);
        
        EXPECT_TRUE(success);
        EXPECT_EQ(decoded, original);
    }
}

TEST(UTF8, SequentialDecoding) {
    UTF8 decoder;
    std::vector<uint32_t> expected = {0x41, 0x42, 0x43};  // 'A', 'B', 'C'
    
    for (uint32_t exp : expected) {
        uint32_t value = 0;
        decoder = UTF8();  // Reset for new character
        int8_t result = decoder.decode(static_cast<uint8_t>(exp), value);
        EXPECT_EQ(result, 1);
        EXPECT_EQ(value, exp);
    }
}

// ============================================================================
// UTF8 Edge cases
// ============================================================================

TEST(UTF8, EncodeMinimumValue) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0);
    
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0);
}

TEST(UTF8, EncodeMaximumValidValue) {
    UTF8 encoder;
    std::vector<uint8_t> result = encoder.encode(0x10FFFF);
    
    EXPECT_GT(result.size(), 0);  // Should be valid
}

TEST(UTF8, DecodeNull) {
    UTF8 decoder;
    uint32_t value = 0;
    int8_t result = decoder.decode(0x00, value);
    
    EXPECT_EQ(result, 1);
    EXPECT_EQ(value, 0);
}

TEST(UTF8, EncodeLatin1SupplementRange) {
    UTF8 encoder;
    
    // Test various characters in Latin-1 Supplement (0x80-0xFF)
    for (uint32_t codepoint = 0x80; codepoint < 0x100; codepoint += 0x10) {
        auto result = encoder.encode(codepoint);
        EXPECT_GT(result.size(), 0);
        EXPECT_LE(result.size(), 2);
    }
}

TEST(UTF8, DecodeSurrogatePair) {
    UTF8 decoder;
    uint32_t value = 0;
    
    // Invalid UTF-8 for surrogate pair
    int8_t result = decoder.decode(0xED, value);
    EXPECT_EQ(result, 0);  // Starts 3-byte sequence
    
    result = decoder.decode(0xA0, value);
    EXPECT_EQ(result, 0);  // Still decoding
    
    result = decoder.decode(0x80, value);
    EXPECT_EQ(result, 1);  // Completes, though value represents surrogate
}

}  // namespace
