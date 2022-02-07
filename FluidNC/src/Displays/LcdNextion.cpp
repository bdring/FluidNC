#include "LcdNextion.h"

#include "../Machine/MachineConfig.h"
#include "WiFi.h"
#include "../WebUI/WebSettings.h"
#include "../WebUI/WifiConfig.h"
#include "../SettingsDefinitions.h"
#include "../Report.h"
#include "../Machine/Axes.h"
#include "../Uart.h"
#include "../InputFile.h"
#include "../Limits.h"  // limits_get_state

static TaskHandle_t lcdUpdateTaskHandle = 0;
static int          geo                 = 0;

namespace Displays {

    Uart* LcdNextion::_uart = nullptr;

    void LcdNextion::init() {
        config_message();
        _uart->config_message("  lcd_nextion", " ");

        xTaskCreatePinnedToCore(timed_update,         // task
                                "nextionUpdateTask",  // name for task
                                4096,                 // size of task stack
                                NULL,                 // parameters
                                1,                    // priority
                                &lcdUpdateTaskHandle,
                                CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
                                                             // core
        );
    }

    // prints the startup message of the spindle config
    void LcdNextion ::config_message() { log_info("Display: " << name()); }

    void LcdNextion::timed_update(void* pvParameters) {
        TickType_t xLcdInterval = 1000;  // in ticks (typically ms)

        vTaskDelay(2500);

        while (true) {
            vTaskDelay(xLcdInterval);
        }
    }

    void LcdNextion::update(UpdateType t, String s = "") {}

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<LcdNextion> registration("lcd_nextion");
    }
}
