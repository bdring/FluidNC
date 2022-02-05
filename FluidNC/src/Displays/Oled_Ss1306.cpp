#include "Oled_Ss1306.h"

#include "../System.h"
#include "../Report.h"

namespace Displays {

    void Oled_Ss1306::init() { config_message(); }

    // prints the startup message of the spindle config
    void Oled_Ss1306 ::config_message() {
        log_info("Display: " << name() << " sda:" << _sda_pin.name() << " scl:" << _scl_pin.name() << " goemetry:" << _geometry);
    }

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<Oled_Ss1306> registration("oled_ss1306");
    }
}
