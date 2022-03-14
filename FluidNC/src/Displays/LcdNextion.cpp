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
        allChannels.registration(_uart);
        _uart->begin();
        _uart_started = true;
        _uart->config_message("  lcd_nextion", " ");
    }

    // prints the startup message of the spindle config
    void LcdNextion ::config_message() { log_info("Display: " << name()); }

    void LcdNextion::update(statusCounter sysCounter) {
        if (!_uart_started)
            return;

        auto& uart = *_uart;

        if (sysCounter.DRO - _statusCount.DRO > 0) {
            sendDROs();
            _statusCount.DRO = sysCounter.DRO;
        }

        if (sysCounter.sysState - _statusCount.sysState > 0) {
            uart << "page0.st0.txt=\"" << state_name() << "\"";
            _uart->write(255);
            _uart->write(255);
            _uart->write(255);
            _statusCount.sysState = sysCounter.sysState;
        }

        if (sysCounter.network - _statusCount.network > 0) {
            _statusCount.network = sysCounter.network;
        }
    }

    void LcdNextion::sendDROs() {
        float* print_position = get_mpos();
        if (bits_are_false(status_mask->get(), RtStatus::Position)) {
            mpos_to_wpos(print_position);
        }

        for (int axis = 0; axis < config->_axes->_numberAxis; axis++) {
            auto& uart = *_uart;
            uart << "page0.dr" << axis << ".val=" << int(print_position[axis] * 1000);
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
