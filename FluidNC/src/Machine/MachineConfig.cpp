// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"

#include "../Kinematics/Kinematics.h"

#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"

#include "../Spindles/NullSpindle.h"
#include "../UartChannel.h"

#include "../SettingsDefinitions.h"  // config_filename
#include "../FileStream.h"

#include "../Configuration/Parser.h"
#include "../Configuration/ParserHandler.h"
#include "../Configuration/Validator.h"
#include "../Configuration/AfterParse.h"
#include "../Configuration/ParseException.h"
#include "../Config.h"  // ENABLE_*

#include "../Maslow/Maslow.h" // using_default_config

#include <cstdio>
#include <cstring>
#include <atomic>

Machine::MachineConfig* config;

// TODO FIXME: Split this file up into several files, perhaps put it in some folder and namespace Machine?

namespace Machine {
    void MachineConfig::group(Configuration::HandlerBase& handler) {
        handler.item("board", _board);
        handler.item("name", _name);
        handler.item("meta", _meta);

        groupM4Items(handler);

        // Maslow M4 - Limited sections
        handler.section("stepping", _stepping);

        handler.section("uart1", _uarts[1], 1);

        // The following could all be commented out and left to defaults from FluidNC
        // uart2, uart_channel1, uart_channel2, i2so, i2c0, i2c1,
        // kinematics,
        // control, coolant, probe, macros, start, parking, user_outputs, oled
        handler.section("uart2", _uarts[2], 2);

        handler.section("uart_channel1", _uart_channels[1]);
        handler.section("uart_channel2", _uart_channels[2]);

        handler.section("i2so", _i2so);

        handler.section("i2c0", _i2c[0], 0);
        handler.section("i2c1", _i2c[1], 1);

        handler.section("spi", _spi);
        handler.section("sdcard", _sdCard);

        handler.section("kinematics", _kinematics);
        handler.section("axes", _axes);

        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);

        handler.section("start", _start);
        handler.section("parking", _parking);

        handler.section("user_outputs", _userOutputs);

        handler.section("oled", _oled);

        Spindles::SpindleFactory::factory(handler, _spindles);

        // Maslow M4 Specific?
        Listeners::SysListenerFactory::factory(handler, _sysListeners);

        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?
        // The following could all be commented out and left to defaults from FluidNC
        handler.item("arc_tolerance_mm", _arcTolerance, 0.001, 1.0);
        handler.item("junction_deviation_mm", _junctionDeviation, 0.01, 1.0);
        handler.item("verbose_errors", _verboseErrors);
        handler.item("report_inches", _reportInches);
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);
        handler.item("use_line_numbers", _useLineNumbers);
        handler.item("planner_blocks", _planner_blocks, 10, 120);
    }

    void MachineConfig::groupM4Items(Configuration::HandlerBase& handler) {
        handler.item(M+"_vertical", Maslow.calibration.orientation);
        handler.item(M+"_calibration_grid_width_mm_X", Maslow.calibration.calibration_grid_width_mm_X, 100, 3000);
        handler.item(M+"_calibration_grid_height_mm_Y", Maslow.calibration.calibration_grid_height_mm_Y, 100, 3000);
        handler.item(M+"_calibration_grid_size", Maslow.calibration.calibrationGridSize, 3, 9);

        handler.item(M+"_tlX", Maslow.tlX);
        handler.item(M+"_tlY", Maslow.tlY);

        handler.item(M+"_trX", Maslow.trX);
        handler.item(M+"_trY", Maslow.trY);

        handler.item(M+"_blX", Maslow.blX);
        handler.item(M+"_blY", Maslow.blY);
        
        handler.item(M+"_brX", Maslow.brX);
        handler.item(M+"_brY", Maslow.brY);

        handler.item(M+"_tlZ", Maslow.tlZ);
        handler.item(M+"_trZ", Maslow.trZ);
        handler.item(M+"_blZ", Maslow.blZ);
        handler.item(M+"_brZ", Maslow.brZ);

        handler.item("Maslow_Retract_Current_Threshold", Maslow.retractCurrentThreshold, 0, 3500);
        handler.item("Maslow_Calibration_Current_Threshold", Maslow.calibrationCurrentThreshold, 0, 3500);
        handler.item("Maslow_Acceptable_Calibration_Threshold", Maslow.acceptableCalibrationThreshold, 0, 1);

        handler.item("Maslow_Scale_X", Maslow.scaleX, .8, 1.2);
        handler.item("Maslow_Scale_Y", Maslow.scaleY, .8, 1.2);

        handler.item(M+"_Retract_Current_Threshold", Maslow.calibration.retractCurrentThreshold, 0, 3500);
        handler.item(M+"_Calibration_Current_Threshold", Maslow.calibration.calibrationCurrentThreshold, 0, 3500);
        handler.item(M+"_Acceptable_Calibration_Threshold", Maslow.calibration.acceptableCalibrationThreshold, 0, 1);
        handler.item(M+"_Extend_Dist", Maslow.calibration.extendDist, 0, 4250);
        handler.item(M+"_beltEndExtension", Maslow._beltEndExtension);
        handler.item(M+"_armLength", Maslow._armLength);

        handler.item(M+"_Scale_X", Maslow.scaleX, .8, 1.2);
        handler.item(M+"_Scale_Y", Maslow.scaleY, .8, 1.2);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_config_error(M+"M4 expects the 'axes' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _axes = new Axes();
        }

        // coolant, kinematics, probe, userOutputs all run with their FluidNC defaults
        if (_coolant == nullptr) {
            _coolant = new CoolantControl();
        }

        if (_kinematics == nullptr) {
            _kinematics = new Kinematics();
        }

        if (_probe == nullptr) {
            _probe = new Probe();
        }

        if (_userOutputs == nullptr) {
            _userOutputs = new UserOutputs();
        }

        if (_sdCard == nullptr) {
            log_config_error(M+" M4 expects the 'scCard' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _sdCard = new SDCard();
        }

        if (_spi == nullptr) {
            log_config_error(M+" M4 expects the 'spi' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _spi = new SPIBus();
        }

        if (_stepping == nullptr) {
            log_config_error(M+" M4 expects the 'stepping' section to be defined in the file or the default config");
            // The following is NOT expected to yield the correct result for the M4
            _stepping = new Stepping();
        }

        // We do not auto-create an I2SO bus config node
        // Only if an i2so section is present will config->_i2so be non-null
        // control, start, parking all run with their FluidNC defaults
        if (_control == nullptr) {
            _control = new Control();
        }

        if (_start == nullptr) {
            _start = new Start();
        }

        if (_parking == nullptr) {
            _parking = new Parking();
        }

        if (_spindles.size() == 0) {
            _spindles.push_back(new Spindles::Null());
        }

        // Precaution in case the full spindle initialization does not happen
        // due to a configuration error
        spindle = _spindles[0];

        uint32_t next_tool = 100;
        for (auto s : _spindles) {
            if (s->_tool == -1) {
                s->_tool = next_tool++;
            }
        }

        // macros runs with its FluidNC defaults
        if (_macros == nullptr) {
            _macros = new Macros();
        }
    }

    // Common Default Config partial strings
    const std::string mcgrid = M+"_calibration_grid_";

    // Individual Default Config items and sections
    // Default Config - Board
    const std::string dcBoard = "name: Default ("+M+" S3 Board)\nboard: "+M+"\n";

    const std::string dcM4Vert = M+"_vertical: false\n";
    const std::string dcM4CalibrationGrid = mcgrid + "width_mm_X: 2000\n" + mcgrid + "height_mm_Y: 1000\n" + mcgrid + "size: 9\n";

    const std::string dcM4Anchors =
        M+"_tlX: -27.6\n"+M+"_tlY: 2064.9\n"
        +M+"_trX: 2924.3\n"+M+"_trY: 2066.5\n"
        +M+"_blX: 0\n"+M+"_blY: 0\n"
        +M+"_brX: 2953.2\n"+M+"_brY: 0\n";
    const std::string dcM4ZAxis = M+"_tlZ: 100\n"+M+"_trZ: 56\n"+M+"_blZ: 34\n"+M+"_brZ: 78\n";

    const std::string dcM4CurrentThreshold = M+"_Retract_Current_Threshold: 1300\n"+M+"_Calibration_Current_Threshold: 1500\n"+M+"_Acceptable_Calibration_Threshold: 0.5\n";

    const std::string dcSpi = "spi:\n  miso_pin: gpio.13\n  mosi_pin: gpio.11\n  sck_pin: gpio.12\n";
    const std::string dcSDCard = "sdcard:\n  card_detect_pin: NO_PIN\n  cs_pin: gpio.10\n";
    const std::string dcStepping = "stepping:\n  engine: RMT\n  idle_ms: 240\n";
    const std::string dcUart1 = "uart1:\n  txd_pin: gpio.1\n  rxd_pin: gpio.2\n  baud: 115200\n  mode: 8N1\n";

    const std::string dcZMotor = 
        "        uart_num: 1\n        cs_pin: NO_PIN\n        r_sense_ohms: 0.110\n"
        "        run_amps: 1.000\n        hold_amps: 0.500\n        microsteps: 0\n"
        "        stallguard: 0\n        stallguard_debug: false\n"
        "        toff_disable: 0\n        toff_stealthchop: 5\n        toff_coolstep: 3\n"
        "        run_mode: StealthChop\n        homing_mode: StealthChop\n        use_enable: true\n";

    const std::string defaultConfig =
        dcBoard +
        // Maslow M4 default items
        dcM4Vert + dcM4CalibrationGrid + dcM4Anchors + dcM4ZAxis + dcM4CurrentThreshold +
        // Default sections
        dcSpi + dcSDCard + dcStepping + dcUart1 + 
        "axes:\n"
        "  x:\n    max_rate_mm_per_min: 2000\n    acceleration_mm_per_sec2: 25\n    max_travel_mm: 2438.4\n    homing:\n      cycle: -1\n"
        "    motor0:\n      dc_servo:\n"
        "  y:\n    max_rate_mm_per_min: 2000\n    acceleration_mm_per_sec2: 25\n    max_travel_mm: 1219.2\n    homing:\n      cycle: -1\n"
        "  z:\n    max_rate_mm_per_min: 400\n    acceleration_mm_per_sec2: 10\n    max_travel_mm: 100\n    steps_per_mm: 100\n    homing:\n      cycle: -1\n"
        "    motor0:\n      tmc_2209:\n        addr: 0\n        direction_pin: gpio.16\n        step_pin: gpio.15\n" + dcZMotor +
        "    motor1:\n      tmc_2209:\n        addr: 1\n        direction_pin: gpio.38\n        step_pin: gpio.46\n" + dcZMotor;

    bool MachineConfig::load() {
        bool configOkay;

        // If the system crashes we skip the config file and use the default
        // builtin config.  This helps prevent reset loops on bad config files.
        esp_reset_reason_t reason = esp_reset_reason();

        // TEST: Uncomment the following to mock an ESP panic reset
        //reason = ESP_RST_PANIC;
        if (reason == ESP_RST_PANIC) {
            log_error("Skipping configuration file due to panic");
            configOkay = false;
        } else {
            configOkay = load_file(config_filename->get());
        }

        if (!configOkay) {
            log_info("Using default configuration");
            configOkay = load_yaml(new StringRange(defaultConfig.c_str()));
            Maslow.using_default_config = true;
        }
        //configOkay = load(config_filename->get());
        return configOkay;
    }

    bool MachineConfig::load_file(const char* filename) {
        try {
            FileStream file(std::string { filename }, "r", "");

            auto filesize = file.size();
            if (filesize <= 0) {
                log_config_error("Configuration file:" << filename << " is empty");
                return false;
            }

            char* buffer     = new char[filesize + 1];
            buffer[filesize] = '\0';
            auto actual      = file.read(buffer, filesize);
            if (actual != filesize) {
                log_config_error("Configuration file:" << filename << " read error");
                return false;
            }
            log_info("Configuration file:" << filename);
            bool retval = load_yaml(new StringRange(buffer, buffer + filesize));
            delete[] buffer;
            return retval;
        } catch (...) {
            log_config_error("Cannot open configuration file:" << filename);
            return false;
        }
    }

    bool MachineConfig::load_yaml(StringRange* input) {
        bool successful = false;
        try {
            Configuration::Parser        parser(input->begin(), input->end());
            Configuration::ParserHandler handler(parser);

            // instance() is by reference, so we can just get rid of an old instance and
            // create a new one here:
            {
                auto& machineConfig = instance();
                if (machineConfig != nullptr) {
                    delete machineConfig;
                }
                machineConfig = new MachineConfig();
            }
            config = instance();

            handler.enterSection("machine", config);

            log_debug("Running after-parse tasks");

            try {
                Configuration::AfterParse afterParse;
                config->afterParse();
                config->group(afterParse);
            } catch (std::exception& ex) { log_error("Validation error: " << ex.what()); }

            log_debug("Checking configuration");

            try {
                Configuration::Validator validator;
                config->validate();
                config->group(validator);
            } catch (std::exception& ex) {
                log_config_error("Validation error: " << ex.what());
            }

            // log_info("Heap size after configuation load is " << uint32_t(xPortGetFreeHeapSize()));

            successful = (sys.state() != State::ConfigAlarm);

            if (!successful) {
                log_config_error("Configuration is invalid");
            }
        } catch (const Configuration::ParseException& ex) {
            sys.set_state(State::ConfigAlarm);
            log_config_error("Configuration parse error on line " << ex.LineNumber() << ": " << ex.What());
        } catch (const AssertionFailed& ex) {
            sys.set_state(State::ConfigAlarm);
            // Get rid of buffer and return
            log_config_error("Configuration loading failed: " << ex.what());
        } catch (std::exception& ex) {
            sys.set_state(State::ConfigAlarm);
            // Log exception:
            log_config_error("Configuration validation error: " << ex.what());
        } catch (...) {
            sys.set_state(State::ConfigAlarm);
            // Get rid of buffer and return
            log_config_error("Unknown error while processing config file");
        }
        delete[] input;

        std::atomic_thread_fence(std::memory_order::memory_order_seq_cst);
        
        return successful;
    }

    MachineConfig::~MachineConfig() {
        delete _axes;
        delete _i2so;
        delete _coolant;
        delete _probe;
        delete _sdCard;
        delete _spi;
        delete _control;
        delete _macros;

        // Maslow M4 specific?
        delete _extenders;

        // Also, what about ...
        /*
        delete _kinematics;
        delete _stepping;
        delete _userOutputs;
        delete _start;
        delete _parking;
        delete _oled;
        */

        // And proper deletion of
        // _i2c[MAX_N_I2C], _uart_channels[MAX_N_UARTS], _uarts[MAX_N_UARTS]
    }
}
