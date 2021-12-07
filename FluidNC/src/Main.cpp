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
#    include "Uart.h"
#    include "MotionControl.h"
#    include "Platform.h"

#    include "WebUI/TelnetServer.h"
#    include "WebUI/Serial2Socket.h"
#    include "WebUI/InputBuffer.h"

#    include "WebUI/WifiConfig.h"
#    include <SPIFFS.h>

extern void make_user_commands();

void setup() {
    try {
        uartInit();  // Setup serial port

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        allChannels.init();

        WebUI::WiFiConfig::reset();

        display_init();

        // Load settings from non-volatile storage
        settings_init();  // requires config

        log_info("FluidNC " << git_info);
        log_info("Compiled with ESP32 SDK:" << ESP.getSdkVersion());

        if (!SPIFFS.begin(true)) {
            log_error("Cannot mount the local filesystem");
        }

        bool configOkay = config->load();

        make_user_commands();

        if (configOkay) {
            log_info("Machine " << config->_name);
            log_info("Board " << config->_board);

            // The initialization order reflects dependencies between the subsystems
            if (config->_i2so) {
                config->_i2so->init();
            }
            if (config->_spi) {
                config->_spi->init();

                if (config->_sdCard != nullptr) {
                    config->_sdCard->init();
                }
            }

            config->_stepping->init();  // Configure stepper interrupt timers

            config->_userOutputs->init();

            config->_axes->init();

            config->_control->init();

            config->_kinematics->init();

            memset(motor_steps, 0, sizeof(motor_steps));  // Clear machine position.

            machine_init();  // user supplied function for special initialization
        }

        // Initialize system state.
        if (sys.state != State::ConfigAlarm) {
            if (FORCE_INITIALIZATION_ALARM) {
                // Force ALARM state upon a power-cycle or hard reset.
                sys.state = State::Alarm;
            } else {
                sys.state = State::Idle;
            }

            limits_init();

            // Check for power-up and set system alarm if homing is enabled to force homing cycle
            // by setting alarm state. Alarm locks out all g-code commands, including the
            // startup scripts, but allows access to settings and internal commands. Only a homing
            // cycle '$H' or kill alarm locks '$X' will disable the alarm.
            // NOTE: The startup script will run after successful completion of the homing cycle, but
            // not after disabling the alarm locks. Prevents motion startup blocks from crashing into
            // things uncontrollably. Very bad.
            if (config->_start->_mustHome && Machine::Axes::homingMask) {
                // If there is an axis with homing configured, enter Alarm state on startup
                sys.state = State::Alarm;
            }
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

    WebUI::wifi_config.begin();
    WebUI::bt_config.begin();
    WebUI::inputBuffer.begin();
}

static void reset_variables() {
#    ifdef DEBUG_STEPPING
    rtTestPl    = false;
    rtTestSt    = false;
    st_seq      = 0;
    st_seq0     = 0;
    pl_seq0     = 0;
    seg_seq0    = 0;
    seg_seq1    = 0;
    planner_seq = 0;
#    endif
    // Reset primary systems.
    system_reset();
    protocol_reset();
    gc_init();  // Set g-code parser to default state
    // Spindle should be set either by the configuration
    // or by the post-configuration fixup, but we test
    // it anyway just for safety.  We want to avoid any
    // possibility of crashing at this point.

    plan_reset();  // Clear block buffer and planner variables

    if (sys.state != State::ConfigAlarm) {
        if (spindle) {
            spindle->stop();
            report_ovr_counter = 0;  // Set to report change immediately
        }
        Stepper::reset();  // Clear stepper subsystem variables
    }

    // Sync cleared gcode and planner positions to current system position.
    plan_sync_position();
    gc_sync_position();
    report_init_message(allChannels);
    mc_init();
}

void loop() {
    static int tries = 0;
    try {
        reset_variables();
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
