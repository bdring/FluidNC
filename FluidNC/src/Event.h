// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <Arduino.h>  // String

class Event {
public:
    Event() {}
    virtual void run(void* arg) = 0;
};

class NoArgEvent : public Event {
    void (*_function)() = nullptr;

public:
    NoArgEvent(void (*function)()) : _function(function) {}
    void run(void* arg) override {
        if (_function) {
            _function();
        }
    }
};

class ArgEvent : public Event {
    void (*_function)(void*) = nullptr;

public:
    ArgEvent(void (*function)(void*)) : _function(function) {}
    void run(void* arg) override {
        if (_function) {
            _function(arg);
        }
    }
};
