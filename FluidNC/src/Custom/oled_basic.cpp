// Copyright (c) 2020 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
    OLED display code.

    It is designed to be used with a machine that has no easily accessible serial connection
    It shows basic status and connection information.

    When in alarm mode it will show the current Wifi/BT paramaters and status
    Most machines will start in alarm mode (needs homing)
    If the machine is running a file job it will show the progress
    In other modes it will show state and 3 axis DROs
    Thats All! 
*/
#include "../Config.h"

#ifdef INCLUDE_OLED_BASIC
#    include "oled_io.h"
#    include "../Main.h"  // display_init()

#    include "../Machine/MachineConfig.h"
#    include "WiFi.h"
#    include "../WebUI/WebSettings.h"
#    include "../WebUI/WifiConfig.h"
#    include "../SettingsDefinitions.h"
#    include "../Report.h"
#    include "../Machine/Axes.h"
#    include "../Uart.h"
#    include "../InputFile.h"

static TaskHandle_t oledUpdateTaskHandle = 0;

// This displays the status of the ESP32 Radios...BT, WiFi, etc
static void oledRadioInfo() {
    String radio_addr   = "";
    String radio_name   = "";
    String radio_status = "";

#    ifdef ENABLE_BLUETOOTH
    if (WebUI::bt_enable->get()) {
        radio_name = String("BT: ") + WebUI::bt_name->get();
        ;
    }
#    endif
#    ifdef ENABLE_WIFI
    if (radio_name == "") {
        if ((WiFi.getMode() == WIFI_MODE_STA) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            radio_name = "STA: " + WiFi.SSID();
            radio_addr = WiFi.localIP().toString();
        } else if ((WiFi.getMode() == WIFI_MODE_AP) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
            radio_name = String("AP:") + WebUI::wifi_ap_ssid->get();
            radio_addr = WiFi.softAPIP().toString();
        } else {
            radio_name = "Radio Mode: None";
        }
    }
#    endif

    if (radio_name == "") {
        radio_name = "Radio Mode:Disabled";
    }

    oled->setTextAlignment(TEXT_ALIGN_LEFT);
    oled->setFont(ArialMT_Plain_10);

    if (sys.state == State::Alarm) {  // print below Alarm:
        oled->drawString(0, 18, radio_name);
        oled->drawString(0, 30, radio_addr);

    } else {  // print next to status
#    ifdef ENABLE_BLUETOOTH
        oled->drawString(55, 2, radio_name);
#    else
        oled->drawString(55, 2, radio_addr);
#    endif
    }
}

static void draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
    if (checked) {
        oled->fillRect(x, y, width, height);  // If log.0
    } else {
        oled->drawRect(x, y, width, height);  // If log.1
    }
}

static void oledDRO() {
    uint8_t oled_y_pos;
    //float   wco[MAX_N_AXIS];

    oled->setTextAlignment(TEXT_ALIGN_LEFT);
    oled->setFont(ArialMT_Plain_10);

    char axisVal[20];

    oled->drawString(80, 14, "L");  // Limit switch

    auto n_axis        = config->_axes->_numberAxis;
    auto ctrl_pins     = config->_control;
    bool prb_pin_state = config->_probe->get_state();

    oled->setTextAlignment(TEXT_ALIGN_RIGHT);

    float* print_position = get_mpos();
    if (bits_are_true(status_mask->get(), RtStatus::Position)) {
        oled->drawString(60, 14, "M Pos");
    } else {
        oled->drawString(60, 14, "W Pos");
        mpos_to_wpos(print_position);
    }

    for (uint8_t axis = X_AXIS; axis < n_axis; axis++) {
        oled_y_pos = 24 + (axis * 10);

        String axis_letter = String(Machine::Axes::_names[axis]);
        axis_letter += ":";
        oled->setTextAlignment(TEXT_ALIGN_LEFT);
        oled->drawString(0, oled_y_pos, axis_letter);  // String('X') + ":");

        oled->setTextAlignment(TEXT_ALIGN_RIGHT);
        snprintf(axisVal, 20 - 1, "%.3f", print_position[axis]);
        oled->drawString(60, oled_y_pos, axisVal);

        //if (bitnum_is_true(limitAxes, axis)) {  // only draw the box if a switch has been defined
        //    draw_checkbox(80, 27 + (axis * 10), 7, 7, limits_check(bitnum_to_mask(axis)));
        //}
    }

    oled_y_pos = 14;

    if (config->_probe->exists()) {
        oled->drawString(110, oled_y_pos, "P");
        draw_checkbox(120, oled_y_pos + 3, 7, 7, prb_pin_state);
        oled_y_pos += 10;
    }
    if (ctrl_pins->_feedHold._pin.defined()) {
        oled->drawString(110, oled_y_pos, "H");
        draw_checkbox(120, oled_y_pos + 3, 7, 7, ctrl_pins->_feedHold.get());
        oled_y_pos += 10;
    }
    if (ctrl_pins->_cycleStart._pin.defined()) {
        oled->drawString(110, oled_y_pos, "S");
        draw_checkbox(120, oled_y_pos + 3, 7, 7, ctrl_pins->_cycleStart.get());
        oled_y_pos += 10;
    }

    if (ctrl_pins->_reset._pin.defined()) {
        oled->drawString(110, oled_y_pos, "R");
        draw_checkbox(120, oled_y_pos + 3, 7, 7, ctrl_pins->_reset.get());
        oled_y_pos += 10;
    }

    if (ctrl_pins->_safetyDoor._pin.defined()) {
        oled->drawString(110, oled_y_pos, "D");
        draw_checkbox(120, oled_y_pos + 3, 7, 7, ctrl_pins->_safetyDoor.get());
        oled_y_pos += 10;
    }
}

static void oledUpdate(void* pvParameters) {
    TickType_t       xLastWakeTime;
    const TickType_t xOledFrequency = 100;                  // in ticks (typically ms)
    xLastWakeTime                   = xTaskGetTickCount();  // Initialise the xLastWakeTime variable with the current time.

    vTaskDelay(2500);
    uint16_t file_ticker = 0;

    oled->init();
    oled->flipScreenVertically();

    while (true) {
        oled->clear();

        String state_string = "";

        oled->setTextAlignment(TEXT_ALIGN_LEFT);
        oled->setFont(ArialMT_Plain_16);
        oled->drawString(0, 0, state_name());

        if (infile) {
            oled->clear();
            oled->setTextAlignment(TEXT_ALIGN_CENTER);
            oled->setFont(ArialMT_Plain_10);
            state_string = "File";
            for (int i = 0; i < file_ticker % 10; i++) {
                state_string += ".";
            }
            file_ticker++;
            oled->drawString(63, 0, state_string);

            oled->drawString(63, 12, infile->path());

            int progress = infile->percent_complete();
            // draw the progress bar
            oled->drawProgressBar(0, 45, 120, 10, progress);

            // draw the percentage as String
            oled->setFont(ArialMT_Plain_10);
            oled->setTextAlignment(TEXT_ALIGN_CENTER);
            oled->drawString(64, 25, String(progress) + "%");

        } else if (sys.state == State::Alarm) {
            oledRadioInfo();
        } else {
            oledDRO();
            oledRadioInfo();
        }

        oled->display();

        vTaskDelayUntil(&xLastWakeTime, xOledFrequency);
    }
}

void display_init() {
    init_oled(0x3c, GPIO_NUM_14, GPIO_NUM_13, GEOMETRY_128_64);

    oled->flipScreenVertically();
    oled->setTextAlignment(TEXT_ALIGN_LEFT);

    oled->clear();
    oled->display();

    xTaskCreatePinnedToCore(oledUpdate,        // task
                            "oledUpdateTask",  // name for task
                            4096,              // size of task stack
                            NULL,              // parameters
                            1,                 // priority
                            &oledUpdateTaskHandle,
                            CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
                                                         // core
    );
}
static void oled_show_string(String s) {
    oled->clear();
    oled->drawString(0, 0, s);
    oled->display();
}

void display(const char* tag, String s) {
    if (!strcmp(tag, "IP")) {
        oled_show_string(s);
        return;
    }
    if (!strcmp(tag, "MACHINE")) {
        // remove characters from the end until the string fits
        while (oled->getStringWidth(s) > 64) {
            s = s.substring(0, s.length() - 1);
        }
        oled_show_string(s);
    }
}
#endif
