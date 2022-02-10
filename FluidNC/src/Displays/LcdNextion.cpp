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
#include "../Limits.h"             // limits_get_state
#include "../WebUI/InputBuffer.h"  // WebUI::inputBuffer

static TaskHandle_t lcdUpdateTaskHandle = 0;
static int          geo                 = 0;

namespace Displays {

    Uart* LcdNextion::_uart         = nullptr;
    bool  LcdNextion::_uart_started = false;

    void LcdNextion::init() {
        config_message();
        _uart->begin();
        _uart_started = true;
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
        TickType_t xLcdInterval = 250;  // in ticks (typically ms)

        vTaskDelay(2500);

        while (true) {
            if (_uart_started) {
                sendDROs();

                int ch;
                while (_uart->available()) {
                    ch = _uart->read();                    
                    if (is_realtime_command(ch)) {
                        WebUI::inputBuffer.push(ch);
                        log_info("RT:" << ch);
                    } else {                        
                        WebUI::inputBuffer.push(ch);
                    }
                }
            }
            vTaskDelay(xLcdInterval);
        }
    }

    void LcdNextion::update(UpdateType t, String s = "") {
        if (!_uart_started)
            return;

        if (t == UpdateType::SysState) {
            // log_info("Update: " << name() << " type:" << (int)t);
            auto& uart = *_uart;
            uart << "st0.txt=\"" << state_name() << "\"";
            _uart->write(255);
            _uart->write(255);
            _uart->write(255);
        }
    }

    void LcdNextion::sendDROs() {
        float* print_position = get_mpos();
        if (bits_are_false(status_mask->get(), RtStatus::Position)) {
            mpos_to_wpos(print_position);
        }

        for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
            auto& uart = *_uart;
            uart << "dr" << axis << ".val=" << int(print_position[axis] * 1000);
            _uart->write(255);
            _uart->write(255);
            _uart->write(255);
        }
    }

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<LcdNextion> registration("lcd_nextion");
    }
}
