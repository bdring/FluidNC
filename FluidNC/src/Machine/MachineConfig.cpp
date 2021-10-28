// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "MachineConfig.h"

#include "../Motors/MotorDriver.h"
#include "../Motors/NullMotor.h"

#include "../Spindles/NullSpindle.h"

#include "../Logging.h"

#include "../Configuration/Parser.h"
#include "../Configuration/ParserHandler.h"
#include "../Configuration/Validator.h"
#include "../Configuration/AfterParse.h"
#include "../Configuration/ParseException.h"
#include "../Config.h"  // ENABLE_*

#include <SPIFFS.h>
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

        handler.section("stepping", _stepping);
        handler.section("axes", _axes);
        handler.section("i2so", _i2so);
        handler.section("spi", _spi);
        handler.section("sdcard", _sdCard);
        handler.section("control", _control);
        handler.section("coolant", _coolant);
        handler.section("probe", _probe);
        handler.section("macros", _macros);
        handler.section("start", _start);

        handler.section("user_outputs", _userOutputs);
        // TODO: Consider putting these under a gcode: hierarchy level? Or motion control?
        handler.item("arc_tolerance_mm", _arcTolerance);
        handler.item("junction_deviation_mm", _junctionDeviation);
        handler.item("verbose_errors", _verboseErrors);
        handler.item("report_inches", _reportInches);
        handler.item("enable_parking_override_control", _enableParkingOverrideControl);
        handler.item("use_line_numbers", _useLineNumbers);

        Spindles::SpindleFactory::factory(handler, _spindles);
    }

    void MachineConfig::afterParse() {
        if (_axes == nullptr) {
            log_info("Axes: using defaults");
            _axes = new Axes();
        }

        if (_coolant == nullptr) {
            _coolant = new CoolantControl();
        }

        if (_probe == nullptr) {
            _probe = new Probe();
        }

        if (_userOutputs == nullptr) {
            _userOutputs = new UserOutputs();
        }

        if (_sdCard == nullptr) {
            _sdCard = new SDCard();
        }

        if (_spi == nullptr) {
            _spi = new SPIBus();
        }

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

        if (_macros == nullptr) {
            _macros = new Macros();
        }
    }

    size_t MachineConfig::readFile(const char* filename, char*& buffer) {
        String path = filename;
        if ((path.length() > 0) && (path[0] != '/')) {
            path = "/" + path;
        }

        File file = SPIFFS.open(path, FILE_READ);

        // There is a subtle problem with the Arduino framework.  If
        // the framework does not find the file, it tries to open the
        // path as a directory.  SPIFFS_opendir(... path ...) always
        // succeeds, regardless of what path is, hence the need to
        // check that it is not a directory.

        if (!file || file.isDirectory()) {
            if (file) {
                file.close();
            }
            log_error("Missing config file " << path);
            return 0;
        }

        auto filesize = file.size();
        if (filesize == 0) {
            log_info("config file " << path << " is empty");
            return 0;
        }
        buffer = new char[filesize + 1];

        size_t pos = 0;
        while (pos < filesize) {
            auto read = file.read((uint8_t*)(buffer + pos), filesize - pos);
            if (read == 0) {
                break;
            }
            pos += read;
        }

        file.close();
        buffer[filesize] = 0;

        if (pos != filesize) {
            delete[] buffer;

            log_error("Cannot read the config file");
            return 0;
        }
        return filesize;
    }

    char defaultConfig[] = "name: Default (Test Drive)\nboard: None\n";

    bool MachineConfig::load(const char* filename) {
        // If the system crashes we skip the config file and use the default
        // builtin config.  This helps prevent reset loops on bad config files.
        size_t             filesize = 0;
        char*              buffer   = nullptr;
        esp_reset_reason_t reason   = esp_reset_reason();
        if (reason == ESP_RST_PANIC) {
            log_debug("Skipping configuration file due to panic");
        } else {
            filesize = readFile(filename, buffer);
        }

        StringRange* input = nullptr;

        if (filesize > 0) {
            input = new StringRange(buffer, buffer + filesize);
            log_info("Configuration file:" << filename);

        } else {
            log_info("Using default configuration");
            input = new StringRange(defaultConfig);
        }
        // Process file:
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
            } catch (std::exception& ex) { log_info("Validation error: " << ex.what()); }

            log_debug("Checking configuration");

            try {
                Configuration::Validator validator;
                config->validate();
                config->group(validator);
            } catch (std::exception& ex) { log_info("Validation error: " << ex.what()); }

            // log_info("Heap size after configuation load is " << uint32_t(xPortGetFreeHeapSize()));

            successful = (sys.state != State::ConfigAlarm);

            if (!successful) {
                log_info("Configuration is invalid");
            }

        } catch (const Configuration::ParseException& ex) {
            sys.state      = State::ConfigAlarm;
            auto startNear = ex.Near();
            auto endNear   = (startNear + 10) > (buffer + filesize) ? (buffer + filesize) : (startNear + 10);

            auto startKey = ex.KeyStart();
            auto endKey   = ex.KeyEnd();

            StringRange near(startNear, endNear);
            StringRange key(startKey, endKey);
            log_error("Configuration parse error: " << ex.What() << " @ " << ex.LineNumber() << ":" << ex.ColumnNumber() << " key "
                                                    << key.str() << " near " << near.str());
        } catch (const AssertionFailed& ex) {
            sys.state = State::ConfigAlarm;
            // Get rid of buffer and return
            log_error("Configuration loading failed: " << ex.what());
        } catch (std::exception& ex) {
            sys.state = State::ConfigAlarm;
            // Log exception:
            log_error("Configuration validation error: " << ex.what());
        } catch (...) {
            sys.state = State::ConfigAlarm;
            // Get rid of buffer and return
            log_error("Unknown error while processing config file");
        }

        if (buffer) {
            delete[] buffer;
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
    }
}
