#include "dac.h"
#include "../Capture.h"

#include <string>
#include <sstream>

void dacWrite(uint8_t pin, uint8_t value) {
    std::ostringstream oss;
    oss << "DAC." << pin;

    Capture::instance().write(oss.str(), value);
}
