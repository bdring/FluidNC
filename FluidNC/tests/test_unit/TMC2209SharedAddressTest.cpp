#include <gtest/gtest.h>

#include "Motors/TMC2209SharedAddress.h"

using MotorDrivers::TMC2209UartSettings;
using MotorDrivers::TMC2209UartEndpoint;

namespace {
    TMC2209UartSettings settings() {
        return { 0.15f, 2.0f, 1.2f, 2.0f, 8, 1, 1, 0, 0, 5, 3, true };
    }
}

TEST(TMC2209SharedAddress, CompatibleUartSettingsAreAccepted) {
    auto first  = settings();
    auto second = settings();
    EXPECT_EQ(first.mismatch(second), nullptr);
}

TEST(TMC2209SharedAddress, SingleDriverRetainsStrictReadbackByDefault) {
    EXPECT_TRUE(MotorDrivers::tmc2209RequiresReadback(false));
}

TEST(TMC2209SharedAddress, WriteOnlyMustBeExplicit) {
    EXPECT_FALSE(MotorDrivers::tmc2209RequiresReadback(true));
}

TEST(TMC2209SharedAddress, DistinctAddressesAreNotGrouped) {
    TMC2209UartEndpoint first { 1, 1, false };
    TMC2209UartEndpoint second { 1, 2, false };
    EXPECT_FALSE(first.sharesUnselectedAddress(second));
}

TEST(TMC2209SharedAddress, SameUnselectedAddressIsGrouped) {
    TMC2209UartEndpoint first { 1, 1, false };
    TMC2209UartEndpoint second { 1, 1, false };
    EXPECT_TRUE(first.sharesUnselectedAddress(second));
}

TEST(TMC2209SharedAddress, ConflictingUartSettingsIdentifyTheField) {
    auto first       = settings();
    auto conflicting = settings();
    conflicting.run_current = 1.5f;
    EXPECT_STREQ(first.mismatch(conflicting), "run_current");
}

TEST(TMC2209SharedAddress, IndependentMotionPinsAreNotGroupSettings) {
    // The settings type intentionally has no step, direction, disable, or
    // limit pins.  Compatible shared drivers therefore retain independent
    // per-motor motion wiring.
    auto first  = settings();
    auto second = settings();
    EXPECT_EQ(first.mismatch(second), nullptr);
}
