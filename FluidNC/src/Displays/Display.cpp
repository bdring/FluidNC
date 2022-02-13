// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    A Base class for spindles and spindle like things such as lasers
*/
#include "Display.h"

namespace Displays {
    // ========================= Spindle ==================================
    void Display::init() {}
    void Display::status_changed() {}
    void Display::update(UpdateType t, String s) {}
    void Display::afterParse() {}
}
