// Copyright (c) 2024 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Interface between Stepping.cpp and low-level stepping engine drivers
// This is in C instead of C++ to make it easy to force the relevant pieces
// to be in RAM or IRAM, thus avoiding ESP32 problems with accessing FLASH
// from interrupt service routines.

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct step_engine {
    const char* name;

    // Prepare the engine for use
    // The return value is the actual pulse delay according to the
    // characteristics of the engine.
    uint32_t (*init)(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t frequency, bool (*fn)(void));

    // Setup the step pin, returning a number to identify it.
    // In many cases, the return value is the same as pin, but some step
    // engines might allocate a surrogate object and return its ID
    int (*init_step_pin)(int pin, int inverted);

    // Set the state of the direction pin to level
    void (*set_dir_pin)(int pin, int level);

    // Commit all of the direction pin changes and wait for dir_delay_us
    // if necessary
    void (*finish_dir)();

    // Set the state of the step pin to level
    void (*start_step)();

    // Set the state of the step pin to level
    void (*set_step_pin)(int pin, int level);

    // Commit all of the direction pin changes and either wait for pulse_delay_us
    // or arrange for start_unstep to do it
    void (*finish_step)();

    // Wait for pulse_delay_us if necessary
    // If the return value is true, Stepping.cpp will skip the rest of the
    // the unstep process
    int (*start_unstep)();

    // Commit all changes (deassertions) of step pins
    void (*finish_unstep)();

    // The maximum step rate for this engine as a function of dir_delay_us,
    // pulse_delay_us, and other characteristics of this stepping engine
    uint32_t (*max_pulses_per_sec)();

    // Set the period to the next pulse event in ticks of the stepping timer
    void (*set_timer_ticks)(uint32_t ticks);

    // Start the pulse event timer
    void (*start_timer)();

    // Stop the pulse event timer
    void (*stop_timer)();

    // Link to next engine in the list of registered stepping engines
    struct step_engine* link;
} step_engine_t;

// Linked list of registered step engines
extern step_engine_t* step_engines;

// clang-format off
#define REGISTER_STEP_ENGINE(name, engine)                       \
    __attribute__((constructor)) void __register_##name(void) {  \
        (engine)->link = step_engines;                           \
        step_engines = engine;                                   \
    }
