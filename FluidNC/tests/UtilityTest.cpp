// Test suite for utility templates and constants (without external dependencies)
#include "gtest/gtest.h"
#include <cstdint>

namespace {

// ============================================================================
// myConstrain template function tests (inline implementation)
// ============================================================================

template <typename T>
T myConstrain(T in, const T min, const T max) {
    if (in < min) {
        return min;
    }
    if (in > max) {
        return max;
    }
    return in;
}

TEST(UtilityTemplates, ConstrainInt32Basic) {
    int32_t result = myConstrain(50, 0, 100);
    EXPECT_EQ(result, 50);
}

TEST(UtilityTemplates, ConstrainInt32BelowMin) {
    int32_t result = myConstrain(-10, 0, 100);
    EXPECT_EQ(result, 0);
}

TEST(UtilityTemplates, ConstrainInt32AboveMax) {
    int32_t result = myConstrain(150, 0, 100);
    EXPECT_EQ(result, 100);
}

TEST(UtilityTemplates, ConstrainInt32AtBoundaries) {
    EXPECT_EQ(myConstrain(0, 0, 100), 0);
    EXPECT_EQ(myConstrain(100, 0, 100), 100);
}

TEST(UtilityTemplates, ConstrainFloat) {
    float result = myConstrain(50.5f, 0.0f, 100.0f);
    EXPECT_FLOAT_EQ(result, 50.5f);
}

TEST(UtilityTemplates, ConstrainFloatBelowMin) {
    float result = myConstrain(-10.5f, 0.0f, 100.0f);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(UtilityTemplates, ConstrainFloatAboveMax) {
    float result = myConstrain(150.5f, 0.0f, 100.0f);
    EXPECT_FLOAT_EQ(result, 100.0f);
}

TEST(UtilityTemplates, ConstrainNegativeRange) {
    EXPECT_EQ(myConstrain(-50, -100, -10), -50);
    EXPECT_EQ(myConstrain(-150, -100, -10), -100);
    EXPECT_EQ(myConstrain(0, -100, -10), -10);
}

TEST(UtilityTemplates, ConstrainSingleValue) {
    int32_t result = myConstrain(50, 50, 50);
    EXPECT_EQ(result, 50);
}

// ============================================================================
// myMap template function tests (inline implementation)
// ============================================================================

template <typename I, typename O>
O myMap(I x, const I in_min, const I in_max, O out_min, O out_max) {
    return static_cast<O>((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);
}

TEST(UtilityTemplates, MapBasic) {
    int32_t result = myMap(50, 0, 100, 0, 1000);
    EXPECT_EQ(result, 500);
}

TEST(UtilityTemplates, MapBoundaries) {
    EXPECT_EQ(myMap(0, 0, 100, 0, 1000), 0);
    EXPECT_EQ(myMap(100, 0, 100, 0, 1000), 1000);
}

TEST(UtilityTemplates, MapQuarters) {
    EXPECT_EQ(myMap(25, 0, 100, 0, 1000), 250);
    EXPECT_EQ(myMap(75, 0, 100, 0, 1000), 750);
}

TEST(UtilityTemplates, MapNegativeRange) {
    EXPECT_EQ(myMap(50, 0, 100, -100, 100), 0);
    EXPECT_EQ(myMap(25, 0, 100, -100, 100), -50);
}

TEST(UtilityTemplates, MapNegativeInput) {
    int32_t result = myMap(-50, -100, 100, 0, 200);
    EXPECT_EQ(result, 50);
}

TEST(UtilityTemplates, MapFloat) {
    float result = myMap(0.5f, 0.0f, 1.0f, 0.0f, 100.0f);
    EXPECT_FLOAT_EQ(result, 50.0f);
}

// ============================================================================
// mapConstrain function tests
// ============================================================================

template <typename I, typename O>
O mapConstrain(I x, const I in_min, const I in_max, O out_min, O out_max) {
    x = myConstrain(x, in_min, in_max);
    return myMap(x, in_min, in_max, out_min, out_max);
}

TEST(UtilityTemplates, MapConstrainBasic) {
    int32_t result = mapConstrain(50, 0, 100, 0, 1000);
    EXPECT_EQ(result, 500);
}

TEST(UtilityTemplates, MapConstrainBelowMin) {
    int32_t result = mapConstrain(-50, 0, 100, 0, 1000);
    EXPECT_EQ(result, 0);
}

TEST(UtilityTemplates, MapConstrainAboveMax) {
    int32_t result = mapConstrain(150, 0, 100, 0, 1000);
    EXPECT_EQ(result, 1000);
}

// ============================================================================
// Conversion constants tests
// ============================================================================

const float MM_PER_INCH = (25.40f);
const float INCH_PER_MM = (0.0393701f);

TEST(UtilityTemplates, ConversionMMPerInch) {
    float inches = 1.0f;
    float mm = inches * MM_PER_INCH;
    EXPECT_FLOAT_EQ(mm, 25.40f);
}

TEST(UtilityTemplates, ConversionInchPerMM) {
    float mm = 25.40f;
    float inches = mm * INCH_PER_MM;
    // Allow for floating point rounding error
    EXPECT_NEAR(inches, 1.0f, 0.000001f);
}

TEST(UtilityTemplates, ConversionRoundTrip) {
    float original_mm = 100.0f;
    float inches = original_mm * INCH_PER_MM;
    float back_to_mm = inches * MM_PER_INCH;
    // Allow for floating point rounding error
    EXPECT_NEAR(back_to_mm, original_mm, 0.0001f);
}

TEST(UtilityTemplates, ConversionKnownValues) {
    // Note: Floating point conversions may have rounding error
    EXPECT_FLOAT_EQ(10.0f * MM_PER_INCH, 254.0f);
    EXPECT_NEAR(254.0f * INCH_PER_MM, 10.0f, 0.00001f);
}

// ============================================================================
// MIN and MAX macro tests (inline implementations)
// ============================================================================

#define TEST_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define TEST_MIN(a, b) (((a) < (b)) ? (a) : (b))

TEST(UtilityTemplates, MaxMacroBasic) {
    EXPECT_EQ(TEST_MAX(5, 10), 10);
    EXPECT_EQ(TEST_MAX(10, 5), 10);
    EXPECT_EQ(TEST_MAX(5, 5), 5);
}

TEST(UtilityTemplates, MaxMacroNegative) {
    EXPECT_EQ(TEST_MAX(-5, -10), -5);
    EXPECT_EQ(TEST_MAX(-10, 5), 5);
}

TEST(UtilityTemplates, MaxMacroFloat) {
    EXPECT_FLOAT_EQ(TEST_MAX(5.5f, 10.5f), 10.5f);
    EXPECT_FLOAT_EQ(TEST_MAX(10.5f, 5.5f), 10.5f);
}

TEST(UtilityTemplates, MinMacroBasic) {
    EXPECT_EQ(TEST_MIN(5, 10), 5);
    EXPECT_EQ(TEST_MIN(10, 5), 5);
    EXPECT_EQ(TEST_MIN(5, 5), 5);
}

TEST(UtilityTemplates, MinMacroNegative) {
    EXPECT_EQ(TEST_MIN(-5, -10), -10);
    EXPECT_EQ(TEST_MIN(-10, 5), -10);
}

TEST(UtilityTemplates, MinMacroFloat) {
    EXPECT_FLOAT_EQ(TEST_MIN(5.5f, 10.5f), 5.5f);
    EXPECT_FLOAT_EQ(TEST_MIN(10.5f, 5.5f), 5.5f);
}

// ============================================================================
// Complex scenarios
// ============================================================================

TEST(UtilityTemplates, ComplexChain) {
    // Test a chain of operations
    int32_t input = 150;  // Out of range
    int32_t constrained = myConstrain(input, 0, 100);  // Should be 100
    int32_t mapped = myMap(constrained, 0, 100, 0, 10);  // Should be 10
    EXPECT_EQ(mapped, 10);
}

TEST(UtilityTemplates, ReverseMap) {
    // Map and reverse map
    int32_t original = 25;
    int32_t forward = myMap(original, 0, 100, 0, 1000);  // 250
    int32_t backward = myMap(forward, 0, 1000, 0, 100);   // Should be 25
    EXPECT_EQ(backward, 25);
}

TEST(UtilityTemplates, ConversionAccuracy) {
    // Test that conversions maintain reasonable accuracy
    // Note: Floating point rounding means we can't use FLOAT_EQ for round-trip conversions
    for (float mm = 0.0f; mm <= 100.0f; mm += 10.0f) {
        float inches = mm * INCH_PER_MM;
        float back_to_mm = inches * MM_PER_INCH;
        // Allow for floating point rounding error
        EXPECT_NEAR(back_to_mm, mm, 0.0001f);
    }
}

}  // namespace
