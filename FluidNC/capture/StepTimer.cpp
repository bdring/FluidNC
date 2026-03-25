// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses direct GPIO accesses timed by spin loops.

#include "Platform.h"
#include "Driver/step_engine.h"
#include <thread>
#include <chrono>

static std::atomic<bool>                                  _running(true);
static std::atomic<bool>                                  _pulsing(false);
static uint32_t                                           _elapsed_us = 0;
static uint32_t                                           _target_us  = 0;
static std::mutex                                         mtx;
static std::condition_variable                            cv;
static std::chrono::time_point<std::chrono::steady_clock> _target_time;
static std::chrono::duration<int64_t, std::micro>         _tick_interval;

static bool (*timer_isr_callback)(void);

static void timing_loop() {
    while (_running) {
        if (_pulsing) {
            _target_time += _tick_interval;
            auto now = std::chrono::steady_clock::now();
            if (_target_time > now) {
                std::this_thread::sleep_for(_target_time - now);
            }
            if (!timer_isr_callback()) {
                _pulsing = false;
            }
        } else {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock);
        }
    }
}

uint32_t stepTimerInit(bool (*callback)(void)) {
    // Note: frequency must be 1000000 (units of microseconds); this implementation uses that frequency
    timer_isr_callback = callback;
    std::thread(timing_loop).detach();
    return STEPPING_FREQUENCY;
}
void stepTimerStart() {
    _tick_interval = std::chrono::microseconds(1);
    _target_time   = std::chrono::steady_clock::now();
    _pulsing       = true;
    cv.notify_one();
}
void stepTimerStop() {
    _pulsing = false;
}

void stepTimerSetTicks(uint32_t ticks) {
    _tick_interval = std::chrono::microseconds(ticks);
};
