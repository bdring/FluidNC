#include "StdTimer.h"
#include <thread>
#include <iostream>

void tic_processor(StdTimer& pt, int microsec) {
    int      tmp_rubs_for_move = 0;
    uint64_t cur_period        = 0;

    for (;;) {
        if (pt.is_stop()) {
            break;
        }
        if (cur_period >= pt.get_timer_tic()) {
            cur_period = 0;
            if (pt.is_enabled()) {
                pt.do_action();
            }
        }
        cur_period++;
        std::this_thread::sleep_for(std::chrono::nanoseconds(microsec));
    }
}

StdTimer::StdTimer(uint32_t microsec, uint8_t timer, uint16_t divider, bool countUp) :
    _microsec(microsec), _timer(timer), _divider(divider), _countUp(countUp), _action(0), _is_stop(true), _enable(false), _interrupt(0) {}

StdTimer::~StdTimer() {
    stop();
}

void StdTimer::start() {
    if (!_is_stop)
        return;
    _is_stop = false;
    set_enable(false);
    std::thread(tic_processor, std::ref(*this), _microsec).detach();
}

void StdTimer::do_action() {
#ifdef FLUIDNC_CONSOLE_DEBUG_TIMER
    static int pulse_count = 0;
    std::cout << pulse_count << " puls\n";
    pulse_count++;
#endif
    if (_action)
        _action();
}

void StdTimer::set_enable(bool enable) {
    _enable = enable;
}

bool StdTimer::is_enabled() {
    return _enable;
}

void StdTimer::set_action(void (*fn)(void)) {
    _action = fn;
}

void StdTimer::set_pulse_tic(uint64_t interruptAt) {
    _interrupt = interruptAt;  // interruptAt / (_microsec * 100);
}
