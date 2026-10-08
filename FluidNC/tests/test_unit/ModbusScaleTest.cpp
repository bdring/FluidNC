// Test suite for the ModbusVFD scale suffix ("rpm%*100", "rpm*60/100", etc.)
#include "gtest/gtest.h"
#include "Spindles/VFD/ModbusScale.h"
#include <cstdint>

using Spindles::VFD::modbus_scale;

namespace {

uint32_t scaled(uint32_t n, std::string_view scale_str, uint32_t maxRPM = 24000) {
    EXPECT_TRUE(modbus_scale(n, scale_str, maxRPM)) << scale_str;
    return n;
}

TEST(ModbusScale, EmptyIsIdentity) {
    EXPECT_EQ(scaled(12345, ""), 12345u);
}

TEST(ModbusScale, MultiplyDivide) {
    EXPECT_EQ(scaled(24000, "*10/60"), 4000u);
    EXPECT_EQ(scaled(400, "*60/100"), 240u);
    EXPECT_EQ(scaled(24000, "*2"), 48000u);
    EXPECT_EQ(scaled(24000, "/60"), 400u);
}

TEST(ModbusScale, Percent) {
    EXPECT_EQ(scaled(12000, "%"), 50u);
    EXPECT_EQ(scaled(24000, "%*100"), 10000u);
    EXPECT_EQ(scaled(12000, "%*100"), 5000u);
}

// 24000 * 100 * 16384 = 39,321,600,000 overflows 32 bits; it used to yield 277.
TEST(ModbusScale, PercentLargeNumeratorNoOverflow) {
    EXPECT_EQ(scaled(24000, "%*16384/100"), 16384u);
    EXPECT_EQ(scaled(12000, "%*16384/100"), 8192u);
    EXPECT_EQ(scaled(2622, "%*16384/100", 2622), 16384u);
}

TEST(ModbusScale, LargeMultiplyNoOverflow) {
    EXPECT_EQ(scaled(4000000000u, "*4/8"), 2000000000u);
}

TEST(ModbusScale, ZeroDivisorRejected) {
    uint32_t n = 24000;
    EXPECT_FALSE(modbus_scale(n, "%", 0));
    EXPECT_EQ(n, 24000u);
    EXPECT_FALSE(modbus_scale(n, "*10/0", 24000));
    EXPECT_FALSE(modbus_scale(n, "/0", 24000));
    EXPECT_EQ(n, 24000u);
}

TEST(ModbusScale, BadNumberRejected) {
    uint32_t n = 24000;
    EXPECT_FALSE(modbus_scale(n, "*x/60", 24000));
    EXPECT_FALSE(modbus_scale(n, "*10/y", 24000));
    EXPECT_FALSE(modbus_scale(n, "/z", 24000));
    EXPECT_EQ(n, 24000u);
}

}  // namespace
