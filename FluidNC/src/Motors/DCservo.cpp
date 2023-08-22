// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    DCservo.cpp

    This allows an Dynamixel servo to be used like any other motor. Servos
    do have limitation in travel and speed, so you do need to respect that.

    Protocol 2

    https://emanual.robotis.com/docs/en/dxl/protocol2/

*/

#include "DCservo.h"

#include "../Machine/MachineConfig.h"
#include "../System.h"   // mpos_to_steps() etc
#include "../Limits.h"   // limitsMinPosition
#include "../Planner.h"  // plan_sync_position()

#include <cstdarg>
#include <cmath>

namespace MotorDrivers {
    TimerHandle_t            DCservo::_timer = nullptr;
    std::vector<DCservo*> DCservo::_instances;
    bool                     DCservo::_has_errors = false;

    int DCservo::_timer_ms = 10;

    uint8_t DCservo::_tx_message[100];  // send to dynamixel
    uint8_t DCservo::_rx_message[50];   // received from dynamixel
    uint8_t DCservo::_msg_index = 0;    // Current length of message being constructed


    void DCservo::init() {

        schedule_update(this, _timer_ms);

        config_message();  // print the config

        // for bulk updating
        _instances.push_back(this);
    }

    void DCservo::config_motor() {

    }

    void DCservo::config_message() {
        log_info("Config messages ran");
        //log_info("    " << name() << " UART" << _uart_num << " id:" << _id << " Count(" << _countMin << "," << _countMax << ")");
    }


    // This motor will not do a standard home to a limit switch (maybe future)
    // If it is in the homing mask it will a quick move to $<axis>/Home/Mpos
    bool DCservo::set_homing_mode(bool isHoming) {
        if (_has_errors) {
            return false;
        }
        return true;    // Cannot do conventional homing
    }

    void DCservo::read_settings() {}

    // sets the PWM to zero. This allows most servos to be manually moved
    void IRAM_ATTR DCservo::set_disable(bool disable) {
        if (_disabled == disable) {
            return;
        }

        _disabled = disable;

    }


    // This is static; it updates the positions of all the Dynamixels on the UART bus
    void DCservo::update_all() {
        if (_has_errors) {
            return;
        }
    }
    void DCservo::update() { 
        Maslow.recomputePID();
     }

    void DCservo::set_location() {}


    // Configuration registration
    namespace {
        MotorFactory::InstanceBuilder<DCservo> registration("dc_servo");
    }
}
