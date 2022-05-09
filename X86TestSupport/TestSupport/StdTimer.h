#pragma once

/* Kernel includes. */
#include <freertos\FreeRTOS.h>

struct hw_timer_s {
    virtual ~hw_timer_s() {}
};

class StdTimer : public hw_timer_s {
public:
    StdTimer(uint32_t microsec, uint8_t timer = 0, uint16_t divider = 1, bool countUp = false);
    ~StdTimer();
    void     start();
    void     set_enable(bool enable);
    bool     is_enabled();
    void     set_pulse_tic(uint64_t interruptAt);
    bool     is_stop() { return _is_stop; }
    uint8_t  get_timer_id() { return _timer; }
    void     set_action(void (*fn)(void));
    void     stop() { _is_stop = true; }
    uint64_t get_timer_tic() { return _interrupt; }
    void     do_action();

public:
    bool _is_stop;

private:
    uint8_t  _timer;
    bool     _enable;
    uint16_t _divider;
    bool     _countUp;
    void (*_action)(void);
    uint64_t _interrupt;
    int      _microsec;
};
