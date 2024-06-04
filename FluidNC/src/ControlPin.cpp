#include "ControlPin.h"

namespace Machine {
    // cppcheck-suppress unusedFunction
    void ControlPin::init() {
        if (_pin.undefined()) {
            return;
        }
        _pin.report(_legend);
        _pin.setAttr(Pin::Attr::Input);
        _pin.registerEvent(static_cast<EventPin*>(this));
    }
};
