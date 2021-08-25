// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Custom code for MPCNC laser module

#ifdef USE_MACHINE_INIT
/*
machine_init() is called when the firmware starts. You can use it to do any
special things your machine needs at startup.
*/
void machine_init() {
    Pin levelShift = Pin::create(LVL_SHIFT_ENABLE);

    // force this on all the time
    log_info("Custom machine_init() Level Shift Enabled");
    levelShift.setAttr(Pin::Attr::Output);
    levelShift.on();
}
#endif
