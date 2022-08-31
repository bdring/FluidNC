#include "ControlPin.h"

namespace Machine {
    void ControlPin::run(void* arg) {
        if (get()) {
            block();
            startTimer();
            _event->run(arg);
        } else {
            reArm();
        }
    }
}
