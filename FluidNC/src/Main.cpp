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
#    include "Limit.h"
#    include "Protocol.h"
#    include "System.h"
#    include "Driver/Console.h"
#    include "MotionControl.h"
#    include "Platform.h"
#    include "StartupLog.h"
#    include "Module.h"

#    include "Driver/localfs.h"

#    include "ToolChangers/atc.h"

extern void make_user_commands();

void setup() {
    platform_preinit();

    set_state(State::Starting);

    try {
        timing_init();

        // Load settings from non-volatile storage
        settings_init();

        Console.init();  // Setup main interaction channel

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();

        // WebUI::WiFiConfig::reset();

        protocol_init();

        make_coordinates();

        log_info("FluidNC " << git_info << " " << git_url);

        if (localfs_mount()) {
            log_error("Cannot mount a local filesystem");
        } else {
            log_info("Local filesystem type is " << localfsName);
        }

        config->load();

        make_user_commands();

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

#    if MAX_N_I2SO
        if (config->_i2so) {
            config->_i2so->init();
        }
#    endif

#    if MAX_N_SPI
        if (config->_spi) {
            config->_spi->init();
#        if MAX_N_SDCARD
            if (config->_sdCard != nullptr) {
                config->_sdCard->init();
            }
#        endif
        }
#    endif

#    if MAX_N_I2C
        for (size_t i = 0; i < MAX_N_I2C; i++) {
            if (config->_i2c[i]) {
                config->_i2c[i]->init();
            }
        }
#    endif

        // We have to initialize the extenders first, before pins are used
        if (config->_extenders) {
            config->_extenders->init();
        }

        auto listeners = Listeners::SysListenerFactory::objects();
        for (auto l : listeners) {
            l->init();
        }

        Stepping::init();  // Configure stepper interrupt timers

        plan_init();

        config->_userOutputs->init();

        config->_userInputs->init();

        Axes::init();

        config->_control->init();

        config->_kinematics->init();

        limits_init();

        // Initialize system state.
        for (auto const& module : Modules()) {
            module->init();
        }
        for (auto const& module : ConfigurableModules()) {
            module->init();
        }

        auto atcs = ATCs::ATCFactory::objects();
        for (auto const& atc : atcs) {
            atc->init();
        }

        if (!state_is(State::ConfigAlarm)) {
            auto spindles = Spindles::SpindleFactory::objects();
            for (auto const& spindle : spindles) {
                spindle->init();
            }
            bool stopped_spindle, new_spindle;
            Spindles::Spindle::switchSpindle(0, spindles, spindle, stopped_spindle, new_spindle);

            config->_coolant->init();
            config->_probe->init();
        }

        make_proxies();

    } catch (std::exception& ex) {
        // Log exception:
        log_config_error("Critical error in main_init: " << ex.what());
    }

    poll_gpios();  // Initial poll to send events for initial pin states

    allChannels.ready();
    allChannels.deregistration(&startupLog);
    protocol_send_event(&startEvent);
}

void loop() {
    vTaskPrioritySet(NULL, 2);
    static size_t tries = 0;
    try {
        // Start the main loop. Processes program inputs and executes them.
        // This can exit on a system abort condition, in which case run_once()
        // is re-executed by an enclosing loop.  It can also exit via a
        // throw that is caught and handled below.
        protocol_main_loop();
    } catch (std::exception& ex) {
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
        log_config_error("Critical error in run_once: " << ex.what());
    }
    // sys.abort is a user-initiated exit via ^x so we don't limit the number of occurrences
    if (!sys.abort() && ++tries > 1) {
        log_info("Stalling due to too many failures");
        while (1) {}
    }
}

void WEAK_LINK machine_init() {}

#endif
