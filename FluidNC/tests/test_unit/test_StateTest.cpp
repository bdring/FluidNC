#include <gtest/gtest.h>

#include "../src/State.h"

// Behavior-focused tests: validate observable decisions made by code that *consumes*
// the `State` enum.

namespace {
    State g_state_for_test = State::Idle;
}

void set_state(State s) {
    g_state_for_test = s;
}

bool state_is(State s) {
    return g_state_for_test == s;
}

TEST(StateBehavior, StateDispatchDrivesObservableBranches) {
    // Exercises code patterns that consume `State` via switch/dispatch.
    auto is_locked = [](State s) {
        switch (s) {
            case State::Alarm:
            case State::ConfigAlarm:
            case State::Critical:
            case State::SafetyDoor:
                return true;
            default:
                return false;
        }
    };

    set_state(State::Idle);
    EXPECT_FALSE(is_locked(g_state_for_test));
    EXPECT_TRUE(state_is(State::Idle));

    set_state(State::Alarm);
    EXPECT_TRUE(is_locked(g_state_for_test));
    EXPECT_TRUE(state_is(State::Alarm));
}
