// Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC
// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#ifndef UNIT_TEST

#    include "Main.h"
#    include "Machine/MachineConfig.h"

#    include "Config.h"
#    include "Report.h"
#    include "Settings.h"
#    include "SettingsDefinitions.h"
#    include "Limits.h"
#    include "Protocol.h"
#    include "System.h"
#    include "UartChannel.h"
#    include "MotionControl.h"
#    include "Platform.h"
#    include "StartupLog.h"

#    include "WebUI/TelnetServer.h"
#    include "WebUI/InputBuffer.h"

#    include "WebUI/WifiConfig.h"
#    include "Driver/localfs.h"

extern void make_user_commands();

void setup() {
    disableCore0WDT();
    try {
        timing_init();
        uartInit();  // Setup serial port

        StartupLog::init();

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();

        WebUI::WiFiConfig::reset();

        display_init();

        protocol_init();

        // Load settings from non-volatile storage
        settings_init();  // requires config

        log_info("FluidNC " << git_info << " " << git_url);
        log_info("Compiled with ESP32 SDK:" << esp_get_idf_version());

        if (localfs_mount()) {
            log_error("Cannot mount a local filesystem");
        } else {
            log_info("Local filesystem type is " << localfsName);
        }

        bool configOkay = config->load();

        make_user_commands();

        if (configOkay) {
            log_info("Machine " << config->_name);
            log_info("Board " << config->_board);

            // The initialization order reflects dependencies between the subsystems
            for (size_t i = 1; i < MAX_N_UARTS; i++) {
                if (config->_uarts[i]) {
                    config->_uarts[i]->begin();
                }
            }
            for (size_t i = 1; i < MAX_N_UARTS; i++) {
                if (config->_uart_channels[i]) {
                    config->_uart_channels[i]->init();
                }
            }

            if (config->_i2so) {
                config->_i2so->init();
            }
            if (config->_spi) {
                config->_spi->init();

                if (config->_sdCard != nullptr) {
                    config->_sdCard->init();
                }
            }
            for (size_t i = 0; i < MAX_N_I2C; i++) {
                if (config->_i2c[i]) {
                    config->_i2c[i]->init();
                }
            }

            if (config->_oled) {
                config->_oled->init();
            }

            if (config->_stat_out) {
                config->_stat_out->init();
            }

            config->_stepping->init();  // Configure stepper interrupt timers

            plan_init();

            config->_userOutputs->init();

            config->_axes->init();

            config->_control->init();

            config->_kinematics->init();

            limits_init();
        }

        // Initialize system state.
        if (sys.state != State::ConfigAlarm) {
            for (auto s : config->_spindles) {
                s->init();
            }
            Spindles::Spindle::switchSpindle(0, config->_spindles, spindle);

            config->_coolant->init();
            config->_probe->init();
        }

    } catch (const AssertionFailed& ex) {
        // This means something is terribly broken:
        log_error("Critical error in main_init: " << ex.what());
        sys.state = State::ConfigAlarm;
    }

    // Try Bluetooth first so its memory can be released if it is disabled
    if (!WebUI::bt_config.begin()) {
        WebUI::wifi_config.begin();
    }

    allChannels.ready();
    allChannels.deregistration(&startupLog);
    protocol_send_event(&startEvent);
}

void loop() {
    vTaskPrioritySet(NULL, 2);
    static int tries = 0;
    try {
        // Start the main loop. Processes program inputs and executes them.
        // This can exit on a system abort condition, in which case run_once()
        // is re-executed by an enclosing loop.  It can also exit via a
        // throw that is caught and handled below.
        protocol_main_loop();
    } catch (const AssertionFailed& ex) {
        // If an assertion fails, we display a message and restart.
        // This could result in repeated restarts if the assertion
        // happens before waiting for input, but that is unlikely
        // because the code in reset_variables() and the code
        // that precedes the input loop has few configuration
        // dependencies.  The safest approach would be to set
        // a "reconfiguration" flag and redo the configuration
        // step, but that would require combining main_init()
        // and run_once into a single control flow, and it would
        // require careful teardown of the existing configuration
        // to avoid memory leaks. It is probably worth doing eventually.
        log_error("Critical error in run_once: " << ex.msg);
        log_error("Stacktrace: " << ex.stackTrace);
        sys.state = State::ConfigAlarm;
    }
    // sys.abort is a user-initiated exit via ^x so we don't limit the number of occurrences
    if (!sys.abort && ++tries > 1) {
        log_info("Stalling due to too many failures");
        while (1) {}
    }
}

void WEAK_LINK machine_init() {}

#    if 0
int main() {
    setup();  // setup()
    while (1) {   // loop()
        loop();
    }
    return 0;
}
#    endif

#endif
