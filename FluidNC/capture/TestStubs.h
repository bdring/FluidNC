#pragma once

#include "Logging.h"
#include "Pin.h"
#include "State.h"

bool atMsgLevel(MsgLevel level);
void set_state(State s);
bool state_is(State s);

namespace TestStubs {
void reset_state(State s = State::Idle);
void set_log_filter_enabled(bool enabled);
}
