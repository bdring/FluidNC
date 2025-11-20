// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"

#include "Kinematics/Kinematics.h"

#include "Motors/MotorDriver.h"
#include "Motors/NullMotor.h"

#include "Spindles/NullSpindle.h"
#include "ToolChangers/atc.h"
#include "Driver/Console.h"

#include "SettingsDefinitions.h"  // config_filename
#include "FileStream.h"

#include "Configuration/Parser.h"
#include "Configuration/ParserHandler.h"
#include "Configuration/Validator.h"
#include "Configuration/AfterParse.h"
#include "Config.h"  // ENABLE_*

#include "Driver/restart.h"

#include <cstdio>
#include <cstring>
#include <atomic>
#include <memory>

Machine::MachineConfig* config;

// TODO FIXME: Split this file up into several files, perhaps put it in some folder and namespace Machine?

namespace Machine {
    void MachineConfig::group(Configuration::HandlerBase& handler) {
        handler.item("board", _board);
        handler.item("name", _name);
        handler.item("meta", _meta);

        handler.section("stepping", _stepping);

        handler.sections("uart", 1, MAX_N_UARTS, true, _uarts);
        handler.sections("uart_channel", 1, MAX_N_UARTS, true, _uart_channels);
#if MAX_N_I2SO
        // We currently support only one I2S bus
        handler.section("i2so", _i2so);
#endif
#if MAX_N_I2SO
        handler.sections("i2c", 0, MAX_N_I2C, false, _i2c);
#endif
#if MAX_N_SPI
        // We currently support only one SPI bus
        handler.section("spi", _spi);
#endif

#if MAX_N_SDCARD
        handler.section("sdcard", _sdCard);
#endif

        handler.section("kinematics", _kinematics);
        handler.section("axes", _axes);

        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);
        handler.section("extenders", _extenders);
        handler.section("start", _start);
        handler.section("parking", _parking);

        handler.section("user_outputs", _userOutputs);
        handler.section("user_inputs", _userInputs);

        ConfigurableModuleFactory::factory(handler);
        ATCs::ATCFactory::factory(handler);
        Spindles::SpindleFactory::factory(handler);
        Listeners::SysListenerFactory::factory(handler);

        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?
        handler.item("arc_tolerance_mm", _arcTolerance, 0.001, 1.0);
        handler.item("junction_deviation_mm", _junctionDeviation, 0.01, 1.0);
        handler.item("verbose_errors", _verboseErrors);
        handler.item("report_inches", _reportInches);
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);
        handler.item("use_line_numbers", _useLineNumbers);
        handler.item("planner_blocks", _planner_blocks, 10, 120);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_info("Axes: using defaults");
            _axes = new Axes();
        }

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

        if (_userInputs == nullptr) {
            _userInputs = new UserInputs();
        }

#if MAX_N_SDCARD
        if (_sdCard == nullptr) {
            _sdCard = new SDCard();
        }
#endif

#if MAX_N_SPI
        if (_spi == nullptr) {
            _spi = new SPIBus();
        }
#endif

        if (_stepping == nullptr) {
            _stepping = new Stepping();
        }

        // We do not auto-create an I2SO bus config node
        // Only if an i2so section is present will config->_i2so be non-null

        if (_control == nullptr) {
            _control = new Control();
        }

        if (_start == nullptr) {
            _start = new Start();
        }

        if (_parking == nullptr) {
            _parking = new Parking();
        }

        auto spindles = Spindles::SpindleFactory::objects();
        if (spindles.size() == 0) {
            spindles.push_back(new Spindles::Null("NoSpindle"));
            //            Spindles::SpindleFactory::add(new Spindles::Null());
        }

        // Precaution in case the full spindle initialization does not happen
        // due to a configuration error
        spindle = spindles[0];

        uint32_t next_tool = 100;
        for (auto s : Spindles::SpindleFactory::objects()) {
            if (s->_tool == -1) {
                s->_tool = next_tool++;
            }
        }

        if (_macros == nullptr) {
            _macros = new Macros();
        }
    }

    const char defaultConfig[] = "name: Default (Test Drive)\nboard: None\n";

    void MachineConfig::load() {
        // If the system crashes we skip the config file and use the default
        // builtin config.  This helps prevent reset loops on bad config files.
        if (restart_was_panic()) {
            log_error("Skipping configuration file due to panic");
            log_info("Using default configuration");
            load_yaml(defaultConfig);
            set_state(State::ConfigAlarm);
        } else {
            load_file(config_filename->get());
        }
    }

    void MachineConfig::load_file(const std::string_view filename) {
        try {
            FileStream file(std::string { filename }, "rb", "");

            auto filesize = file.size();
            if (filesize <= 0) {
                log_config_error("Configuration file:" << filename << " is empty");
                return;
            }

            auto buffer      = std::make_unique<char[]>(filesize + 1);
            buffer[filesize] = '\0';
            auto actual      = file.read(buffer.get(), filesize);
            if (actual != filesize) {
                log_config_error("Configuration file:" << filename << " read error - expected " << filesize << " got " << actual);
                return;
            }
            log_info("Configuration file:" << filename);
            load_yaml(std::string_view { buffer.get(), filesize });
        } catch (...) {
            log_config_error("Cannot open configuration file:" << filename);
            log_info("Using default configuration");
            load_yaml(defaultConfig);
            set_state(State::ConfigAlarm);
        }
    }

    void MachineConfig::load_yaml(std::string_view input) {
        try {
            try {
                Configuration::Parser        parser(input);
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
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration parse error: " << ex.what());
            }

            try {
                Configuration::AfterParse afterParse;
                config->afterParse();
                config->group(afterParse);
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration after-parse error: " << ex.what());
            }

            try {
                log_debug("Checking configuration");

                Configuration::Validator validator;
                config->validate();
                config->group(validator);

                // log_info("Heap size after configuration load is " << uint32_t(xPortGetFreeHeapSize()));
            } catch (std::exception& ex) {
                // Log exception:
                log_config_error("Configuration validation error: " << ex.what());
            }

        } catch (...) {
            // Get rid of buffer and return
            log_config_error("Unknown error while processing config file");
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    MachineConfig::~MachineConfig() {
        delete _axes;
#if MAX_N_I2SO
        delete _i2so;
#endif
        delete _coolant;
        delete _probe;
#if MAX_N_SDCARD
        delete _sdCard;
#endif
#if MAX_N_SDCARD
        delete _spi;
#endif
        delete _control;
        delete _macros;
    }
}
