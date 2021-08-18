/*
  Grbl.cpp - Initialization and main loop for Grbl
  Part of Grbl
  Copyright (c) 2014-2016 Sungeun K. Jeon for Gnea Research LLC

	2018 -	Bart Dring This file was modifed for use on the ESP32
					CPU. Do not use this with Grbl for atMega328P

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Main.h"
#include "Version.h"
#include "Machine/MachineConfig.h"

#include "Config.h"
#include "Report.h"
#include "Settings.h"
#include "SettingsDefinitions.h"
#include "Limits.h"
#include "Protocol.h"
#include "System.h"
#include "Uart.h"
#include "MotionControl.h"
#include "Platform.h"

#include "WebUI/WifiConfig.h"
#include "WebUI/InputBuffer.h"

#ifdef ENABLE_WIFI
#    include <WiFi.h>
#endif
#include <SPIFFS.h>

extern void make_grbl_commands();

void main_init() {
    try {
        uartInit();  // Setup serial port

#ifdef ENABLE_WIFI
        WiFi.persistent(false);
        WiFi.disconnect(true);
        WiFi.enableSTA(false);
        WiFi.enableAP(false);
        WiFi.mode(WIFI_OFF);
#endif

        display_init();

        // Load Grbl settings from non-volatile storage
        settings_init();  // requires config

        log_info("Grbl_ESP32 Ver " << VERSION << " Date " << VERSION_BUILD);  // print grbl_esp32 verion info
        log_info("Compiled with ESP32 SDK:" << ESP.getSdkVersion());          // print the SDK version

        if (!SPIFFS.begin(true)) {
            log_error("Cannot mount the local filesystem");
        }

        bool configOkay = config->load(config_filename->get());
        make_grbl_commands();

        // Setup input polling loop after loading the configuration,
        // because the polling may depend on the config
        client_init();

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

            Stepper::init();  // Configure stepper pins and interrupt timers

            config->_axes->init();

            config->_control->init();
            config->_userOutputs->init();

            memset(motor_steps, 0, sizeof(motor_steps));  // Clear machine position.

            machine_init();  // user supplied function for special initialization
        }

        // Initialize system state.
        if (sys.state != State::ConfigAlarm) {
            if (FORCE_INITIALIZATION_ALARM) {
                // Force Grbl into an ALARM state upon a power-cycle or hard reset.
                sys.state = State::Alarm;
            } else {
                sys.state = State::Idle;
            }

            limits_init();

            // Check for power-up and set system alarm if homing is enabled to force homing cycle
            // by setting Grbl's alarm state. Alarm locks out all g-code commands, including the
            // startup scripts, but allows access to settings and internal commands. Only a homing
            // cycle '$H' or kill alarm locks '$X' will disable the alarm.
            // NOTE: The startup script will run after successful completion of the homing cycle, but
            // not after disabling the alarm locks. Prevents motion startup blocks from crashing into
            // things uncontrollably. Very bad.
            if (config->_homingInitLock && Machine::Axes::homingMask) {
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

        WebUI::wifi_config.begin();
        if (config->_comms->_bluetoothConfig) {
            config->_comms->_bluetoothConfig->begin();
        }
        WebUI::inputBuffer.begin();
    } catch (const AssertionFailed& ex) {
        // This means something is terribly broken:
        log_info("Critical error in main_init: " << ex.what());
        sys.state = State::ConfigAlarm;
    }
}

static void reset_variables() {
    // Reset Grbl primary systems.
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
    report_init_message(CLIENT_ALL);
    mc_init();
}

void run_once() {
    static int tries = 0;
    try {
        reset_variables();
        // Start Grbl main loop. Processes program inputs and executes them.
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
    // This is inside a loop in Grbl_Esp32.ino
}

void WEAK_LINK machine_init() {}

void WEAK_LINK display_init() {}

/*
  setup() and loop() in the Arduino .ino implements this control flow:

  void main() {
     init();          // setup()
     while (1) {      // loop()
         run_once();
     }
  }
*/
