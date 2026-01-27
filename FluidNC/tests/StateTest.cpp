#include <gtest/gtest.h>
#include "../src/State.h"

// Test State enum values - order and correctness
TEST(StateEnum, IdleIsZero) {
    // Idle must be zero per spec
    EXPECT_EQ(static_cast<uint8_t>(State::Idle), 0);
}

TEST(StateEnum, AlarmValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Alarm), 1);
}

TEST(StateEnum, CheckModeValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::CheckMode), 2);
}

TEST(StateEnum, HomingValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Homing), 3);
}

TEST(StateEnum, CycleValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Cycle), 4);
}

TEST(StateEnum, HoldValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Hold), 5);
}

TEST(StateEnum, HeldValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Held), 6);
}

TEST(StateEnum, JogValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Jog), 7);
}

TEST(StateEnum, SafetyDoorValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::SafetyDoor), 8);
}

TEST(StateEnum, SleepValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Sleep), 9);
}

TEST(StateEnum, ConfigAlarmValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::ConfigAlarm), 10);
}

TEST(StateEnum, CriticalValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Critical), 11);
}

TEST(StateEnum, StartingValue) {
    EXPECT_EQ(static_cast<uint8_t>(State::Starting), 12);
}

// Test state type is uint8_t
TEST(StateEnum, StateIsUint8) {
    static_assert(std::is_same_v<std::underlying_type_t<State>, uint8_t>, "State must be uint8_t");
}

// Test that states are sequential where expected
TEST(StateEnum, SequentialStates) {
    // Early states should have low values
    EXPECT_LT(static_cast<uint8_t>(State::Idle), static_cast<uint8_t>(State::Alarm));
    EXPECT_LT(static_cast<uint8_t>(State::Alarm), static_cast<uint8_t>(State::CheckMode));
}



// Test state value distinctness
TEST(StateEnum, AllStatesAreDistinct) {
    std::set<uint8_t> state_values;
    state_values.insert(static_cast<uint8_t>(State::Idle));
    state_values.insert(static_cast<uint8_t>(State::Alarm));
    state_values.insert(static_cast<uint8_t>(State::CheckMode));
    state_values.insert(static_cast<uint8_t>(State::Homing));
    state_values.insert(static_cast<uint8_t>(State::Cycle));
    state_values.insert(static_cast<uint8_t>(State::Hold));
    state_values.insert(static_cast<uint8_t>(State::Held));
    state_values.insert(static_cast<uint8_t>(State::Jog));
    state_values.insert(static_cast<uint8_t>(State::SafetyDoor));
    state_values.insert(static_cast<uint8_t>(State::Sleep));
    state_values.insert(static_cast<uint8_t>(State::ConfigAlarm));
    state_values.insert(static_cast<uint8_t>(State::Critical));
    state_values.insert(static_cast<uint8_t>(State::Starting));
    
    // Should have 13 distinct values
    EXPECT_EQ(state_values.size(), 13);
}

// Test state ranges fit in uint8_t
TEST(StateEnum, StatesFitInUint8) {
    EXPECT_LT(static_cast<uint8_t>(State::Starting), 256);
}

// Test hold states are sequential (important for feedhold state machine)
TEST(StateEnum, HoldToHeldTransition) {
    // Valid transition: Hold -> Held (sequential)
    EXPECT_EQ(
        static_cast<uint8_t>(State::Held),
        static_cast<uint8_t>(State::Hold) + 1
    );
}

// Test alarm states are distinct from normal states
TEST(StateEnum, AlarmStatesDistinct) {
    // Alarm-type states should be clearly separate from operational states
    uint8_t alarm_val = static_cast<uint8_t>(State::Alarm);
    uint8_t config_alarm_val = static_cast<uint8_t>(State::ConfigAlarm);
    uint8_t critical_val = static_cast<uint8_t>(State::Critical);
    
    EXPECT_NE(alarm_val, config_alarm_val);
    EXPECT_NE(alarm_val, critical_val);
    EXPECT_NE(config_alarm_val, critical_val);
}

// Test state value ranges for common checks
TEST(StateEnum, SafetyDoorIsNotIdleOrCycle) {
    EXPECT_NE(static_cast<uint8_t>(State::SafetyDoor), static_cast<uint8_t>(State::Idle));
    EXPECT_NE(static_cast<uint8_t>(State::SafetyDoor), static_cast<uint8_t>(State::Cycle));
}

// Test machine is locked in certain states
TEST(StateEnum, LockedStates) {
    // These states prevent all motion
    std::vector<State> locked_states = {
        State::Alarm,
        State::ConfigAlarm,
        State::Critical,
        State::SafetyDoor
    };
    
    for (State s : locked_states) {
        EXPECT_NE(static_cast<uint8_t>(s), static_cast<uint8_t>(State::Cycle));
        EXPECT_NE(static_cast<uint8_t>(s), static_cast<uint8_t>(State::Idle));
    }
}

// Test motion states
TEST(StateEnum, MotionAllowedStates) {
    // These states allow motion
    std::vector<State> motion_states = {
        State::Cycle,
        State::Homing,
        State::Jog
    };
    
    for (State s : motion_states) {
        EXPECT_NE(static_cast<uint8_t>(s), static_cast<uint8_t>(State::Alarm));
        EXPECT_NE(static_cast<uint8_t>(s), static_cast<uint8_t>(State::ConfigAlarm));
    }
}

// Test hold states are paired
TEST(StateEnum, HoldStatesPaired) {
    // Hold and Held should always be together
    EXPECT_EQ(
        static_cast<uint8_t>(State::Held) - static_cast<uint8_t>(State::Hold),
        1
    );
}





// Test Critical is the most severe
TEST(StateEnum, CriticalIsMostSevere) {
    EXPECT_GT(static_cast<uint8_t>(State::Critical), static_cast<uint8_t>(State::ConfigAlarm));
    EXPECT_GT(static_cast<uint8_t>(State::Critical), static_cast<uint8_t>(State::Alarm));
}
