#include "ControlPin.h"

namespace Machine {
    void ControlPin::run(void* arg) {
        if (get()) {
            // log_debug(_legend);
            block();
            startTimer();
            _event->run(arg);
        } else {
            reArm();
        }
    }
}
