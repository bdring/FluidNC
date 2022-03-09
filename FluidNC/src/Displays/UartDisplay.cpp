#include "UartDisplay.h"

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

namespace Displays {

    Uart* UartDisplay::_uart         = nullptr;
    bool  UartDisplay::_uart_started = false;

    void UartDisplay::init() {
        config_message();
        allChannels.registration(_uart);
        _uart->begin();
        _uart_started = true;
        _uart->config_message("  uart_display", " ");

        _lastDROUpdate = getCpuTicks();
    }

    // prints the startup message of the spindle config
    void UartDisplay ::config_message() { log_info("Display: " << name()); }

    void UartDisplay::timed_update(void* pvParameters) {}

    void UartDisplay::update(statusCounter sysCounter) {
        if (!_uart_started)
            return;

        auto& uart = *_uart;

        if (_statusCount.sysState < sysCounter.sysState) {
            uart << "New State:" << state_name() << "\r\n";
            _statusCount.sysState = sysCounter.sysState;
        }

        if (_statusCount.DRO < sysCounter.DRO) {
            report_realtime_status(uart);     // should be a
            _statusCount.DRO = sysCounter.DRO;  // catch up
        }
    }

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<UartDisplay> registration("uart_display");
    }

}
