// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "TMC5160ProDriver.h"
#include "../Machine/MachineConfig.h"
#include <atomic>

namespace MotorDrivers {

    void TMC5160ProDriver::init() {
        uint8_t cs_id;
        cs_id = setupSPI();

        // rsense is not used with registers, but needed for the TMCStepper Lib
        tmc5160 = new TMC5160Stepper(cs_id, TMC5160_RSENSE_DEFAULT, _spi_index);

        // use slower speed if I2S
        if (_cs_pin.capabilities().has(Pin::Capabilities::I2S)) {
            tmc5160->setSPISpeed(_spi_freq);
        }
        registration();
    }

    void TMC5160ProDriver::config_motor() {
        tmc5160->begin();
        TrinamicBase::config_motor();
    }

    bool TMC5160ProDriver::test() { return checkVersion(0x30, tmc5160->version()); }

    void TMC5160ProDriver::set_registers(bool isHoming) {
        if (_has_errors) {
            return;
        }

        tmc5160->CHOPCONF(CHOPCONF);
        tmc5160->COOLCONF(COOLCONF);
        tmc5160->THIGH(THIGH);
        tmc5160->TCOOLTHRS(TCOOLTHRS);
        tmc5160->GCONF(GCONF);
        tmc5160->PWMCONF(PWMCONF);
        tmc5160->IHOLD_IRUN(IHOLD_IRUN);
    }

    // Report diagnostic and tuning info
    void TMC5160ProDriver::debug_message() {
        if (_has_errors) {
            return;
        }

        uint32_t tstep = tmc5160->TSTEP();

        if (tstep == 0xFFFFF || tstep < 1) {  // if axis is not moving return
            return;
        }
        float feedrate = Stepper::get_realtime_rate();  //* settings.microsteps[axis_index] / 60.0 ; // convert mm/min to Hz

        log_info(axisName() << " Stallguard " << tmc5160->stallguard() << "   SG_Val:" << tmc5160->sg_result() << " Rate:" << feedrate
                            << " mm/min SG_Setting:" << constrain(_stallguard, -64, 63));
    }

    void TMC5160ProDriver::set_disable(bool disable) {
        if (TrinamicSpiDriver::startDisable(disable)) {
            if (_use_enable) {  // use the register to disable the driver
                tmc5160->toff(TrinamicSpiDriver::toffValue());
            }
        }
    }

    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<TMC5160ProDriver> registration("tmc_5160Pro");
    }
}
