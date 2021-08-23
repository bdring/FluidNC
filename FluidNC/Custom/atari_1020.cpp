// Copyright (c) 2018 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// This contains all the special features required to control an
// Atari 1010 Pen Plotter

// This file is enabled by defining CUSTOM_CODE_FILENAME "atari_1020.cpp"
// in Machines/atari_1020.h, thus causing this file to be included
// from ../custom_code.cpp
#include "Pins/LedcPin.h"

#define HOMING_PHASE_FULL_APPROACH 0   // move to right end
#define HOMING_PHASE_CHECK 1           // check reed switch
#define HOMING_PHASE_RETRACT 2         // retract
#define HOMING_PHASE_SHORT_APPROACH 3  // retract

static TaskHandle_t solenoidSyncTaskHandle = 0;
static TaskHandle_t atariHomingTaskHandle  = 0;
int8_t              solenoid_pwm_chan_num;
uint16_t            solenoid_pull_count;
bool                atari_homing = false;
uint8_t             homing_phase = HOMING_PHASE_FULL_APPROACH;
uint8_t             current_tool;

void machine_init() {
    solenoid_pull_count = 0;  // initialize
    log_info("Atari 1020 Solenoid");

    // setup PWM channel
    solenoid_pwm_chan_num = ledcInit(SOLENOID_PEN_PEN, -1, SOLENOID_PWM_FREQ, SOLENOID_PWM_RES_BITS);
    pinMode(REED_SW_PIN, INPUT_PULLUP);  // external pullup required
    // setup a task that will calculate solenoid position
    xTaskCreatePinnedToCore(solenoidSyncTask,    // task
                            "solenoidSyncTask",  // name for task
                            4096,                // size of task stack
                            NULL,                // parameters
                            1,                   // priority
                            &solenoidSyncTaskHandle,
                            CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
                                                         // core
    );
    // setup a task that will do the custom homing sequence
    xTaskCreatePinnedToCore(atari_home_task,    // task
                            "atari_home_task",  // name for task
                            4096,               // size of task stack
                            NULL,               // parameters
                            1,                  // priority
                            &atariHomingTaskHandle,
                            CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
                                                         // core
    );
}

// this task tracks the Z position and sets the solenoid
void solenoidSyncTask(void* pvParameters) {
    int32_t          current_position[N_AXIS];  // copy of current location
    TickType_t       xLastWakeTime;
    const TickType_t xSolenoidFrequency = SOLENOID_TASK_FREQ;   // in ticks (typically ms)
    xLastWakeTime                       = xTaskGetTickCount();  // Initialise the xLastWakeTime variable with the current time.
    while (true) {
        // don't ever return from this or the task dies
        calc_solenoid(get_mpos()[Z_AXIS]);  // calculate kinematics and move the servos
        vTaskDelayUntil(&xLastWakeTime, xSolenoidFrequency);
    }
}

// to do...have this return a true or false. This could be used by the normal homing feature to
// continue with regular homing after setup
// return true if this completes homing

bool user_defined_homing(AxisMask cycle_mask) {
    // create and start a task to do the special homing
    homing_phase = HOMING_PHASE_FULL_APPROACH;
    atari_homing = true;
    return true;  // this does it...skip the rest of mc_homing_cycle(...)
}

/*
	Do a custom homing routine.

	A task is used because it needs to wait until until idle after each move.

	1) Do a full travel move to the right. OK to stall if the pen started closer
	2) Check for pen 1
	3) If fail Retract
	4) move to right end
	5) Check...
	....repeat up to 12 times to try to find pen one

	TODO can the retract, move back be 1 phase rather than 2?

*/
void atari_home_task(void* pvParameters) {
    uint8_t          homing_attempt = 0;  // how many times have we tried to home
    TickType_t       xLastWakeTime;
    const TickType_t xHomingTaskFrequency = 100;  // in ticks (typically ms) .... need to make sure there is enough time to get out of idle
    char             gcode_line[20];
    while (true) {
        // this task will only last as long as it is homing
        if (atari_homing) {
            // must be in idle or alarm state
            if (sys.state == State::Idle) {
                switch (homing_phase) {
                    case HOMING_PHASE_FULL_APPROACH:           // a full width move to insure it hits left end
                        WebUI::inputBuffer.push("G90G0Z1\r");  // lift the pen
                        sprintf(gcode_line, "G91G0X%3.2f\r", -ATARI_PAPER_WIDTH + ATARI_HOME_POS - 3.0);  // plus a little extra
                        WebUI::inputBuffer.push(gcode_line);
                        homing_attempt = 1;
                        homing_phase   = HOMING_PHASE_CHECK;
                        break;
                    case HOMING_PHASE_CHECK:  // check the limits switch
                        if (digitalRead(REED_SW_PIN) == 0) {
                            // see if reed switch is grounded
                            WebUI::inputBuffer.push("G4P0.1\n");  // dramatic pause
                            motor_steps[X_AXIS] = mpos_to_steps(ATARI_HOME_POS, X_AXIS);
                            motor_steps[Y_AXIS] = 0;
                            motor_steps[Z_AXIS] = mpos_to_steps(1.0, Z_AXIS);
                            gc_sync_position();
                            plan_sync_position();
                            sprintf(gcode_line, "G90G0X%3.2f\r", ATARI_PAPER_WIDTH);  // alway return to right side to reduce home travel stalls
                            WebUI::inputBuffer.push(gcode_line);
                            current_tool  = 1;  // local copy for reference...until actual M6 change
                            gc_state.tool = current_tool;
                            atari_homing  = false;  // done with homing sequence
                        } else {
                            homing_phase = HOMING_PHASE_RETRACT;
                            homing_attempt++;
                        }
                        break;
                    case HOMING_PHASE_RETRACT:
                        sprintf(gcode_line, "G0X%3.2f\r", -ATARI_HOME_POS);
                        WebUI::inputBuffer.push(gcode_line);
                        sprintf(gcode_line, "G0X%3.2f\r", ATARI_HOME_POS);
                        WebUI::inputBuffer.push(gcode_line);
                        homing_phase = HOMING_PHASE_CHECK;
                        break;
                    default:
                        log_info("Homing phase error " << homing_phase);
                        atari_homing = false;

                        // kills task
                        break;
                }
                if (homing_attempt > ATARI_HOMING_ATTEMPTS) {
                    // try all positions plus 1
                    log_info("Atari homing failed");
                    WebUI::inputBuffer.push("G90\r");
                    atari_homing = false;
                }
            }
        }
        vTaskDelayUntil(&xLastWakeTime, xHomingTaskFrequency);
    }
}

// calculate and set the PWM value for the servo
void calc_solenoid(float penZ) {
    bool        isPenUp;
    static bool previousPenState = false;
    uint32_t    solenoid_pen_pulse_len;                     // duty cycle of solenoid
    isPenUp = ((penZ > 0) || (sys.state == State::Alarm));  // is pen above Z0 or is there an alarm
    // if the state has not change, we only count down to the pull time
    if (previousPenState == isPenUp) {
        // if state is unchanged
        if (solenoid_pull_count > 0) {
            solenoid_pull_count--;
            solenoid_pen_pulse_len = SOLENOID_PULSE_LEN_PULL;  // stay at full power while counting down
        } else {
            solenoid_pen_pulse_len = SOLENOID_PULSE_LEN_HOLD;  // pull in delay has expired so lower duty cycle
        }
    } else {
        // pen direction has changed
        solenoid_pen_pulse_len = SOLENOID_PULSE_LEN_PULL;  // go to full power
        solenoid_pull_count    = SOLENOID_PULL_DURATION;   // set the time to count down
    }
    previousPenState = isPenUp;  // save the prev state
    digitalWrite(SOLENOID_DIRECTION_PIN, isPenUp);
    // skip setting value if it is unchanged
    if (ledcRead(solenoid_pwm_chan_num) == solenoid_pen_pulse_len)
        return;
    // update the PWM value
    // ledcWrite appears to have issues with interrupts, so make this a critical section
    portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&myMutex);
    ledcWrite(solenoid_pwm_chan_num, solenoid_pen_pulse_len);
    portEXIT_CRITICAL(&myMutex);
}

/*
	A tool (pen) change is done by bumping the carriage against the right edge 3 times per
	position change. Pen 1-4 is valid range.
*/
void user_tool_change(uint8_t new_tool) {
    uint8_t move_count;
    char    gcode_line[20];
    protocol_buffer_synchronize();  // wait for all previous moves to complete
    if ((new_tool < 1) || (new_tool > MAX_PEN_NUMBER)) {
        log_info("Requested Pen#" << new_tool << " is out of 1-4 range");
        return;
    }
    if (new_tool == current_tool)
        return;
    if (new_tool > current_tool)
        move_count = BUMPS_PER_PEN_CHANGE * (new_tool - current_tool);
    else
        move_count = BUMPS_PER_PEN_CHANGE * ((MAX_PEN_NUMBER - current_tool) + new_tool);
    sprintf(gcode_line, "G0Z%3.2f\r", ATARI_TOOL_CHANGE_Z);  // go to tool change height
    WebUI::inputBuffer.push(gcode_line);
    for (uint8_t i = 0; i < move_count; i++) {
        sprintf(gcode_line, "G0X%3.2f\r", ATARI_HOME_POS);  //
        WebUI::inputBuffer.push(gcode_line);
        WebUI::inputBuffer.push("G0X0\r");
    }
    current_tool = new_tool;
    log_info("Change to Pen#" << current_tool);
}

// move from current tool to next tool....
void atari_next_pen() {
    gc_state.tool = current_tool < MAX_PEN_NUMBER ? current_tool + 1 : 1;
    user_tool_change(gc_state.tool);
}

// Polar coaster has macro buttons, this handles those button pushes.
void user_defined_macro(uint8_t index) {
    char gcode_line[20];
    switch (index) {
        case 0:
            log_info("Pen switch");
            WebUI::inputBuffer.push("$H\r");
            break;
        case 1:
            log_info("Color switch");
            atari_next_pen();
            sprintf(gcode_line, "G90G0X%3.2f\r", ATARI_PAPER_WIDTH);  // alway return to right side to reduce home travel stalls
            WebUI::inputBuffer.push(gcode_line);
            break;
        case 2:
            log_info("Paper switch");
            WebUI::inputBuffer.push("G0Y-25\r");
            WebUI::inputBuffer.push("G4P0.1\r");  // sync...forces wait for planner to clear
            motor_steps[Y_AXIS] = 0;              // reset the Y position
            gc_sync_position();
            plan_sync_position();
            break;
        default:
            break;
    }
}

void user_m30() {
    char gcode_line[20];
    sprintf(gcode_line, "G90G0X%3.2f\r", ATARI_PAPER_WIDTH);  //
    WebUI::inputBuffer.push(gcode_line);
}
