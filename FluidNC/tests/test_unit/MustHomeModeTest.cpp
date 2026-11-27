#include <gtest/gtest.h>

#include "Machine/MustHomeMode.h"

TEST(MustHomeMode, AlarmUnlockOnlyClearsHomingInLegacyModes) {
    EXPECT_TRUE(Machine::alarmUnlockMayClearHoming(Machine::MustHomeMode::Disabled));
    EXPECT_TRUE(Machine::alarmUnlockMayClearHoming(Machine::MustHomeMode::Enabled));
    EXPECT_FALSE(Machine::alarmUnlockMayClearHoming(Machine::MustHomeMode::Strict));
}
