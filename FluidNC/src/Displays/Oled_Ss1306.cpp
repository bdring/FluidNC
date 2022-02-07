#include "Oled_Ss1306.h"

#include "../Machine/MachineConfig.h"
#include "WiFi.h"
#include "../WebUI/WebSettings.h"
#include "../WebUI/WifiConfig.h"
#include "../SettingsDefinitions.h"
#include "../Report.h"
#include "../Machine/Axes.h"
#include "../Uart.h"
#include "../InputFile.h"
#include "../Limits.h"  // limits_get_state
#include <SSD1306Wire.h>

static TaskHandle_t oledUpdateTaskHandle = 0;
static int          geo                  = 0;

namespace Displays {

    SSD1306Wire* oled;

    void Oled_Ss1306::init() {
        if (_sda_pin.undefined() || _scl_pin.undefined()) {
            log_error("oled_ss1306: sda and scl pins must be defined");
            return;
        }

        if (!_sda_pin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("sda pin has incorrect capabilities");
            return;
        }

        if (!_scl_pin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
            log_info("scl pin has incorrect capabilities");
            return;
        }

        if (_geometry != GEOMETRY_128_64 && _geometry != GEOMETRY_64_48) {
            log_info("Display geometry value invalid (0,2)");
            return;
        }

        pinnum_t sda = _sda_pin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        pinnum_t scl = _scl_pin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);

        geo = _geometry;  // copy to static

        switch (_geometry) {
            case GEOMETRY_128_64:
                oled = new SSD1306Wire(0x3c, sda, scl, GEOMETRY_128_64, I2C_ONE, 400000);
                break;
            case GEOMETRY_64_48:
                oled = new SSD1306Wire(0x3c, sda, scl, GEOMETRY_64_48, I2C_ONE, 400000);
                break;
            default:
                break;
        }

        oled->init();

        if (_flip)
            oled->flipScreenVertically();

        oled->clear();
        oled->setTextAlignment(TEXT_ALIGN_LEFT);

        switch (_geometry) {
            case GEOMETRY_128_64:
                oled->setFont(ArialMT_Plain_16);
                oled->drawString(0, 0, "STARTING");
                oled->setFont(ArialMT_Plain_24);
                oled->drawString(0, 20, "FluidNC");
                break;
            case GEOMETRY_64_48:
                // The initial circle is a good indication of a recent reboot
                oled->setFont(ArialMT_Plain_16);
                oled->drawString(0, 0, "STARTING");
                oled->drawString(0, 20, "FluidNC");
                break;
            default:
                break;
        }

        oled->display();

        config_message();

        xTaskCreatePinnedToCore(timed_update,      // task
                                "oledUpdateTask",  // name for task
                                4096,              // size of task stack
                                NULL,              // parameters
                                1,                 // priority
                                &oledUpdateTaskHandle,
                                CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
                                                             // core
        );
    }

    // prints the startup message of the spindle config
    void Oled_Ss1306 ::config_message() {
        log_info("Display: " << name() << " sda:" << _sda_pin.name() << " scl:" << _scl_pin.name() << " goemetry:" << _geometry);
    }

    void Oled_Ss1306::timed_update(void* pvParameters) {
        TickType_t xOledInterval = 1000;  // in ticks (typically ms)

        vTaskDelay(2500);

        while (true) {
            oled->clear();
            switch (geo) {
                case GEOMETRY_128_64:
                    update_128x64();
                    break;
                case GEOMETRY_64_48:
                    update_64x48();
                    break;
                default:
                    break;
            }
            oled->display();
            vTaskDelay(xOledInterval);
        }
    }

    void Oled_Ss1306::update(UpdateType t, String s = "") {
        switch (t) {
            case UpdateType::SysState:
                oled->clear();
                switch (geo) {
                    case GEOMETRY_128_64:
                        update_128x64();
                        break;
                    case GEOMETRY_64_48:
                        update_64x48();
                        break;
                    default:
                        break;
                }
                oled->display();
                break;
                default:
                    break;
        }
    }

    void Oled_Ss1306::update_64x48() {
        String state_string = "File:";

        if (!infile) {
            state_string = state_name();
        }
        oled->setTextAlignment(TEXT_ALIGN_LEFT);
        oled->setFont(ArialMT_Plain_16);
        oled->drawString(0, 0, state_string);

        if (infile) {
            int progress = infile->percent_complete();
            oled->drawProgressBar(0, 18, 32, 10, progress);
            // draw the percentage as String
            oled->setFont(ArialMT_Plain_10);
            oled->setTextAlignment(TEXT_ALIGN_CENTER);
            oled->drawString(40, 18, String(progress) + "%");
        } else {
            radioInfo();
        }
    }

    void Oled_Ss1306::update_128x64() {
        uint16_t file_ticker  = 0;
        String   state_string = "";

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
            oled->drawProgressBar(0, 45, 120, 10, progress);

            // draw the percentage as String
            oled->setFont(ArialMT_Plain_10);
            oled->setTextAlignment(TEXT_ALIGN_CENTER);
            oled->drawString(64, 25, String(progress) + "%");
        } else if (sys.state == State::Alarm) {
            radioInfo();
        } else {
            DRO();
            radioInfo();
        }
    }

    void Oled_Ss1306::radioInfo() {
        String radio_addr   = "";
        String radio_name   = "";
        String radio_status = "";

#ifdef ENABLE_BLUETOOTH
        if (WebUI::bt_enable->get()) {
            radio_name = String("BT: ") + WebUI::bt_name->get();
        }
#endif
#ifdef ENABLE_WIFI
        if (radio_name == "") {
            if ((WiFi.getMode() == WIFI_MODE_STA) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
                radio_name = "STA: " + WiFi.SSID();
                radio_addr = WiFi.localIP().toString();
            } else if ((WiFi.getMode() == WIFI_MODE_AP) || (WiFi.getMode() == WIFI_MODE_APSTA)) {
                radio_name = String("AP:") + WebUI::wifi_ap_ssid->get();
                radio_addr = WiFi.softAPIP().toString();
            } else {
                radio_name = "WiFi: Off";
            }
        }
#endif

        if (radio_name == "") {
            radio_name = "Radio Mode:Disabled";
        }

        if (geo == GEOMETRY_128_64) {
            oled->setTextAlignment(TEXT_ALIGN_LEFT);
            oled->setFont(ArialMT_Plain_10);

            if (sys.state == State::Alarm) {  // print below Alarm:
                oled->drawString(0, 18, radio_name);
                oled->drawString(0, 30, radio_addr);

            } else {  // print next to status
#ifdef ENABLE_BLUETOOTH
                oled->drawString(55, 2, radio_name);
#else
                if (WiFi.getMode() == WIFI_MODE_NULL) {  // $Wifi/Mode=Off
                    oled->drawString(55, 2, radio_name);
                } else {
                    oled->drawString(55, 2, radio_addr);
                }
#endif
            }
        } else if (geo == GEOMETRY_64_48) {
            oled->setTextAlignment(TEXT_ALIGN_LEFT);
            oled->setFont(ArialMT_Plain_16);
#ifdef ENABLE_BLUETOOTH
            oled->drawString(30, 2, radio_name);
#else
            if (WiFi.getMode() == WIFI_MODE_NULL) {  // $Wifi/Mode=Off
                oled->drawString(2, 18, radio_name);
            } else {
                oled->drawString(2, 18, radio_addr);
            }
#endif
        }
    }

    void Oled_Ss1306::DRO() {
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

        MotorMask lim_pin_state = limits_get_state();

        for (uint8_t axis_index = X_AXIS; axis_index < n_axis; axis_index++) {
            oled_y_pos = 24 + (axis_index * 10);

            auto axis = config->_axes->_axis[axis_index];

            String axis_letter = String(Machine::Axes::_names[axis_index]);
            axis_letter += ":";
            oled->setTextAlignment(TEXT_ALIGN_LEFT);
            oled->drawString(0, oled_y_pos, axis_letter);  // String('X') + ":");

            oled->setTextAlignment(TEXT_ALIGN_RIGHT);
            snprintf(axisVal, 20 - 1, "%.3f", print_position[axis_index]);
            oled->drawString(60, oled_y_pos, axisVal);

            if (axis->motorsWithSwitches() != 0) {
                if (bitnum_is_true(lim_pin_state, axis_index) || bitnum_is_true(lim_pin_state, axis_index + 16)) {
                    draw_checkbox(80, oled_y_pos + 3, 7, 7, true);
                } else {
                    draw_checkbox(80, oled_y_pos + 3, 7, 7, false);
                }
            }
        }

        oled_y_pos = 14;

        if (config->_probe->exists()) {
            oled->drawString(110, oled_y_pos, "P");
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

    void Oled_Ss1306::draw_checkbox(int16_t x, int16_t y, int16_t width, int16_t height, bool checked) {
        if (checked) {
            oled->fillRect(x, y, width, height);  // If log.0
        } else {
            oled->drawRect(x, y, width, height);  // If log.1
        }
    }

    // Configuration registration
    namespace {
        DisplayFactory::InstanceBuilder<Oled_Ss1306> registration("oled_ss1306");
    }
}
