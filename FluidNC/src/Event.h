// Copyright (c) 2022 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#include <Arduino.h>  // String

class Event {
    void (*_function)() = nullptr;

public:
    Event() {}
    Event(void (*function)()) : _function(function) {}
    virtual void run() {
        if (_function) {
            _function();
        }
    }
};
