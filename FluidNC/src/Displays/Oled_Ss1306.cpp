#include "Oled_Ss1306.h"

#include "../Main.h"  // display_init()

#include "../Machine/MachineConfig.h"
#include "WiFi.h"
#include "../WebUI/WebSettings.h"
#include "../WebUI/WifiConfig.h"
#include "../SettingsDefinitions.h"
#include "../Report.h"
#include "../Machine/Axes.h"
#include "../Uart.h"
#include "../InputFile.h"
#include <SSD1306Wire.h>

static TaskHandle_t oledUpdateTaskHandle = 0;

namespace Displays {

    SSD1306Wire* oled;

    void Oled_Ss1306::init() {
        if (_sda_pin.undefined() || _scl_pin.undefined()) {
            log_error("oled_ss1306: sda and scl pins must be defined");
            return;
        }

        oled = new SSD1306Wire(0x3c, GPIO_NUM_14, GPIO_NUM_13, GEOMETRY_128_64, I2C_ONE, 400000);

        oled->init();

        if (_flip)
            oled->flipScreenVertically();

        oled->setTextAlignment(TEXT_ALIGN_LEFT);

        oled->clear();

        oled->setFont(ArialMT_Plain_16);
        oled->drawString(0, 0, "STARTING");
        oled->setFont(ArialMT_Plain_24);
        oled->drawString(0, 20, "FluidNC");

        oled->display();

        config_message();
    }

    // prints the startup message of the spindle config
    void Oled_Ss1306 ::config_message() {
        log_info("Display: " << name() << " sda:" << _sda_pin.name() << " scl:" << _scl_pin.name() << " goemetry:" << _geometry);
    }

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<Oled_Ss1306> registration("oled_ss1306");
    }
}
