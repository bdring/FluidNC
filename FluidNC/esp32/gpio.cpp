// Copyright 2022 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "src/Pin.h"
#include "src/Uart.h"
#include "Driver/fluidnc_gpio.h"

#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "src/Protocol.h"

#include <vector>

static gpio_dev_t* _gpio_dev = GPIO_HAL_GET_HW(GPIO_PORT_0);

void IRAM_ATTR gpio_write(pinnum_t pin, bool value) {
    gpio_ll_set_level(_gpio_dev, (gpio_num_t)pin, value);
}
bool IRAM_ATTR gpio_read(pinnum_t pin) {
    return gpio_ll_get_level(_gpio_dev, (gpio_num_t)pin);
}
void gpio_mode(pinnum_t pin, bool input, bool output, bool pullup, bool pulldown, bool opendrain) {
    gpio_config_t conf = { .pin_bit_mask = (1ULL << pin), .intr_type = GPIO_INTR_DISABLE };

    if (input) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_INPUT);
    }
    if (output) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_OUTPUT);
    }
    if (pullup) {
        conf.pull_up_en = GPIO_PULLUP_ENABLE;
    }
    if (pulldown) {
        conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }
    if (opendrain) {
        conf.mode = (gpio_mode_t)((int)conf.mode | GPIO_MODE_DEF_OD);
    }
    gpio_config(&conf);
}
#if 0
void gpio_add_interrupt(pinnum_t pin, int mode, void (*callback)(void*), void* arg) {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);  // Will return an err if already called

    gpio_num_t gpio = (gpio_num_t)pin;
    gpio_isr_handler_add(gpio, callback, arg);

    //FIX interrupts on peripherals outputs (eg. LEDC,...)
    //Enable input in GPIO register
    gpio_hal_context_t gpiohal;
    gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);
    gpio_hal_input_enable(&gpiohal, gpio);
}
void gpio_remove_interrupt(pinnum_t pin) {
    gpio_num_t gpio = (gpio_num_t)pin;
    gpio_isr_handler_remove(gpio);  //remove handle and disable isr for pin
    gpio_set_intr_type(gpio, GPIO_INTR_DISABLE);
}
void gpio_route(pinnum_t pin, uint32_t signal) {
    if (pin == 255) {
        return;
    }
    gpio_num_t gpio = (gpio_num_t)pin;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
    gpio_set_direction(gpio, (gpio_mode_t)GPIO_MODE_DEF_OUTPUT);
    gpio_matrix_out(gpio, signal, 0, 0);
}
#endif

typedef uint64_t gpio_mask_t;

// Can be used to display gpio_mask_t data for debugging
static const char* g_to_hex(gpio_mask_t n) {
    static char hexstr[24];
    snprintf(hexstr, 22, "0x%llx", n);
    return hexstr;
}

static gpio_mask_t gpios_inverted = 0;  // GPIOs that are active low
static gpio_mask_t gpios_interest = 0;  // GPIOs with an action
static gpio_mask_t gpios_current  = 0;  // The last GPIO action events that were sent

static int32_t gpio_next_event_ticks[GPIO_NUM_MAX + 1] = { 0 };
static int32_t gpio_deltat_ticks[GPIO_NUM_MAX + 1]     = { 0 };

// Do not send events for changes that occur too soon
static void gpio_set_rate_limit(int gpio_num, uint32_t ms) {
    gpio_deltat_ticks[gpio_num] = ms * portTICK_PERIOD_MS;
}

static inline gpio_mask_t get_gpios() {
    return ((((uint64_t)REG_READ(GPIO_IN1_REG)) << 32) | REG_READ(GPIO_IN_REG)) ^ gpios_inverted;
}
static gpio_mask_t gpio_mask(int gpio_num) {
    return 1ULL << gpio_num;
}
static inline bool gpio_is_active(int gpio_num) {
    return get_gpios() & gpio_mask(gpio_num);
}
static void gpios_update(gpio_mask_t& gpios, int gpio_num, bool active) {
    if (active) {
        gpios |= gpio_mask(gpio_num);
    } else {
        gpios &= ~gpio_mask(gpio_num);
    }
}

static gpio_dispatch_t gpioActions[GPIO_NUM_MAX + 1] = { nullptr };
static void*           gpioArgs[GPIO_NUM_MAX + 1];

void gpio_set_action(int gpio_num, gpio_dispatch_t action, void* arg, bool invert) {
    gpioActions[gpio_num] = action;
    gpioArgs[gpio_num]    = arg;
    gpio_mask_t mask      = gpio_mask(gpio_num);
    gpios_update(gpios_interest, gpio_num, true);
    gpios_update(gpios_inverted, gpio_num, invert);
    gpio_set_rate_limit(gpio_num, 5);
    bool active = gpio_is_active(gpio_num);

    // Set current to the opposite of the current state so the first poll will send the current state
    gpios_update(gpios_current, gpio_num, !active);
}
void gpio_clear_action(int gpio_num) {
    gpioActions[gpio_num] = nullptr;
    gpioArgs[gpio_num]    = nullptr;
    gpios_update(gpios_interest, gpio_num, false);
}

static void gpio_send_action(int gpio_num, bool active) {
    auto    end_ticks  = gpio_next_event_ticks[gpio_num];
    int32_t this_ticks = int32_t(xTaskGetTickCount());
    if (end_ticks == 0 || ((this_ticks - end_ticks) > 0)) {
        end_ticks = this_ticks + gpio_deltat_ticks[gpio_num];
        if (end_ticks == 0) {
            end_ticks = 1;
        }
        gpio_next_event_ticks[gpio_num] = end_ticks;

        gpio_dispatch_t action = gpioActions[gpio_num];
        if (action) {
            action(gpio_num, gpioArgs[gpio_num], active);
        }
        gpios_update(gpios_current, gpio_num, active);
    }
}

void poll_gpios() {
    gpio_mask_t gpios_active  = get_gpios();
    gpio_mask_t gpios_changed = (gpios_active ^ gpios_current) & gpios_interest;
    if (gpios_changed) {
        int zeros;
        while ((zeros = __builtin_clzll(gpios_changed)) != 64) {
            int gpio_num = 63 - zeros;
            gpio_send_action(gpio_num, gpios_active & gpio_mask(gpio_num));
            // Remove bit from mask so clzll will find the next one
            gpios_update(gpios_changed, gpio_num, false);
        }
    }
}

// Support functions for gpio_dump
static bool exists(gpio_num_t gpio) {
    if (gpio == 20) {
        // GPIO20 is listed in GPIO_PIN_MUX_REG[] but it is only
        // available on the ESP32-PICO-V3 package.
        return false;
    }
    return GPIO_PIN_MUX_REG[gpio];  // Missing GPIOs have 0 entries in this array
}
static bool output_level(gpio_num_t gpio) {
    if (gpio < 32) {
        return REG_READ(GPIO_OUT_REG) & (1 << gpio);
    } else {
        return REG_READ(GPIO_OUT1_REG) & (1 << (gpio - 32));
    }
}

static bool is_input(gpio_num_t gpio) {
    return GET_PERI_REG_MASK(GPIO_PIN_MUX_REG[gpio], FUN_IE);
}
static bool is_output(gpio_num_t gpio) {
    if (gpio < 32) {
        return GET_PERI_REG_MASK(GPIO_ENABLE_REG, 1 << gpio);
    } else {
        return GET_PERI_REG_MASK(GPIO_ENABLE1_REG, 1 << (gpio - 32));
    }
}
static int gpio_function(gpio_num_t gpio) {
    return REG_GET_FIELD(GPIO_PIN_MUX_REG[gpio], MCU_SEL);
}
static uint32_t gpio_out_sel(gpio_num_t gpio) {
    return REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + (gpio * 4));
}
static uint32_t gpio_in_sel(int function) {
    return REG_READ(GPIO_FUNC0_IN_SEL_CFG_REG + (function * 4));
}

// another way to determine available gpios is the array GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT]
// which has 0 in unavailable slots, see soc/gpio_periph.{ch}
std::vector<int> avail_gpios = { 0, 1, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39 };

struct pin_mux {
    int         pinnum;
    const char* pinname;
    const char* functions[6];
} const pins[] = {
    { 0, "GPIO0", { "GPIO0", "CLK_OUT1", "GPIO0", "-", "-", "EMAC_TX_CLK" } },
    { 1, "U0TXD", { "U0TXD", "CLK_OUT3", "GPIO1", "-", "-", "EMAC_RXD2" } },
    { 2, "GPIO2", { "GPIO2", "HSPIWP", "GPIO2", "HS2_DATA0", "SD_DATA0", "-" } },
    { 3, "U0RXD", { "U0RXD", "CLK_OUT2", "GPIO3", "-", "-", "-" } },
    { 4, "GPIO4", { "GPIO4", "HSPIHD", "GPIO4", "HS2_DATA1", "SD_DATA1", "EMAC_TX_ER" } },
    { 5, "GPIO5", { "GPIO5", "VSPICS0", "GPIO5", "HS1_DATA6", "-", "EMAC_RX_CLK" } },
    { 6, "SD_CLK", { "SD_CLK", "SPICLK", "GPIO6", "HS1_CLK", "U1CTS", "-" } },
    { 7, "SD_DATA_0", { "SD_DATA0", "SPIQ", "GPIO7", "HS1_DATA0", "U2RTS", "-" } },
    { 8, "SD_DATA_1", { "SD_DATA1", "SPID", "GPIO8", "HS1_DATA1", "U2CTS", "-" } },
    { 9, "SD_DATA_2", { "SD_DATA2", "SPIHD", "GPIO9", "HS1_DATA2", "U1RXD", "-" } },
    { 10, "SD_DATA_3", { "SD_DATA3", "SPIWP", "GPIO10", "HS1_DATA3", "U1TXD", "-" } },
    { 11, "SD_CMD", { "SD_CMD", "SPICS0", "GPIO11", "HS1_CMD", "U1RTS", "-" } },
    { 12, "MTDI", { "MTDI", "HSPIQ", "GPIO12", "HS2_DATA2", "SD_DATA2", "EMAC_TXD3" } },
    { 13, "MTCK", { "MTCK", "HSPID", "GPIO13", "HS2_DATA3", "SD_DATA3", "EMAC_RX_ER" } },
    { 14, "MTMS", { "MTMS", "HSPICLK", "GPIO14", "HS2_CLK", "SD_CLK", "EMAC_TXD2" } },
    { 15, "MTDO", { "MTDO", "HSPICS0", "GPIO15", "HS2_CMD", "SD_CMD", "EMAC_RXD3" } },
    { 16, "GPIO16", { "GPIO16", "-", "GPIO16", "HS1_DATA4", "U2RXD", "EMAC_CLK_OUT1" } },
    { 17, "GPIO17", { "GPIO17", "-", "GPIO17", "HS1_DATA5", "U2TXD", "EMAC_CLK_1801" } },
    { 18, "GPIO18", { "GPIO18", "VSPICLK", "GPIO18", "HS1_DATA7", "-", "-" } },
    { 19, "GPIO19", { "GPIO19", "VSPIQ", "GPIO19", "U0CTS", "-", "EMAC_TXD0" } },
    { 21, "GPIO21", { "GPIO21", "VSPIHD", "GPIO21", "-", "-", "EMAC_TX_EN" } },
    { 22, "GPIO22", { "GPIO22", "VSPIWP", "GPIO22", "U0RTS", "-", "EMAC_TXD1" } },
    { 23, "GPIO23", { "GPIO23", "VSPID", "GPIO23", "HS1_STROBE", "-", "-" } },
    { 25, "GPIO25", { "GPIO25", "-", "GPIO25", "-", "-", "EMAC_RXD0" } },
    { 26, "GPIO26", { "GPIO26", "-", "GPIO26", "-", "-", "EMAC_RXD1" } },
    { 27, "GPIO27", { "GPIO27", "-", "GPIO27", "-", "-", "EMAC_RX_DV" } },
    { 32, "32K_XP", { "GPIO32", "-", "GPIO32", "-", "-", "-" } },
    { 33, "32K_XN", { "GPIO33", "-", "GPIO33", "-", "-", "-" } },
    { 34, "VDET_1", { "GPIO34", "-", "GPIO34", "-", "-", "-" } },
    { 35, "VDET_2", { "GPIO35", "-", "GPIO35", "-", "-", "-" } },
    { 36, "SENSOR_VP", { "GPIO36", "-", "GPIO36", "-", "-", "-" } },
    { 37, "SENSOR_CAPP", { "GPIO37", "-", "GPIO37", "-", "-", "-" } },
    { 38, "SENSOR_CAPN", { "GPIO38", "-", "GPIO38", "-", "-", "-" } },
    { 39, "SENSOR_VN", { "GPIO39", "-", "GPIO39", "-", "-", "-" } },
    { -1, "", { "" } },
};
const char* pin_function_name(gpio_num_t gpio, int function) {
    const pin_mux* p;
    for (p = pins; p->pinnum != -1; ++p) {
        if (p->pinnum == gpio) {
            return p->functions[function];
        }
    }
    return "";
}

struct gpio_matrix_t {
    int         num;
    const char* in;
    const char* out;
    bool        iomux;
} const gpio_matrix[] = { { 0, "SPICLK_in", "SPICLK_out", true },
                    { 1, "SPIQ_in", "SPIQ_out", true },
                    { 2, "SPID_in", "SPID_out", true },
                    { 3, "SPIHD_in", "SPIHD_out", true },
                    { 4, "SPIWP_in", "SPIWP_out", true },
                    { 5, "SPICS0_in", "SPICS0_out", true },
                    { 6, "SPICS1_in", "SPICS1_out", false },
                    { 7, "SPICS2_in", "SPICS2_out", false },
                    { 8, "HSPICLK_in", "HSPICLK_out", true },
                    { 9, "HSPIQ_in", "HSPIQ_out", true },
                    { 10, "HSPID_in", "HSPID_out", true },
                    { 11, "HSPICS0_in", "HSPICS0_out", true },
                    { 12, "HSPIHD_in", "HSPIHD_out", true },
                    { 13, "HSPIWP_in", "HSPIWP_out", true },
                    { 14, "U0RXD_in", "U0TXD_out", true },
                    { 15, "U0CTS_in", "U0RTS_out", true },
                    { 16, "U0DSR_in", "U0DTR_out", false },
                    { 17, "U1RXD_in", "U1TXD_out", true },
                    { 18, "U1CTS_in", "U1RTS_out", true },
                    { 23, "I2S0O_BCK_in", "I2S0O_BCK_out", false },
                    { 24, "I2S1O_BCK_in", "I2S1O_BCK_out", false },
                    { 25, "I2S0O_WS_in", "I2S0O_WS_out", false },
                    { 26, "I2S1O_WS_in", "I2S1O_WS_out", false },
                    { 27, "I2S0I_BCK_in", "I2S0I_BCK_out", false },
                    { 28, "I2S0I_WS_in", "I2S0I_WS_out", false },
                    { 29, "I2CEXT0_SCL_in", "I2CEXT0_SCL_out", false },
                    { 30, "I2CEXT0_SDA_in", "I2CEXT0_SDA_out", false },
                    { 31, "pwm0_sync0_in", "sdio_tohost_int_out", false },
                    { 32, "pwm0_sync1_in", "pwm0_out0a", false },
                    { 33, "pwm0_sync2_in", "pwm0_out0b", false },
                    { 34, "pwm0_f0_in", "pwm0_out1a", false },
                    { 35, "pwm0_f1_in", "pwm0_out1b", false },
                    { 36, "pwm0_f2_in", "pwm0_out2a", false },
                    { 37, "", "pwm0_out2b", false },
                    { 39, "pcnt_sig_ch0_in0", "", false },
                    { 40, "pcnt_sig_ch1_in0", "", false },
                    { 41, "pcnt_ctrl_ch0_in0", "", false },
                    { 42, "pcnt_ctrl_ch1_in0", "", false },
                    { 43, "pcnt_sig_ch0_in1", "", false },
                    { 44, "pcnt_sig_ch1_in1", "", false },
                    { 45, "pcnt_ctrl_ch0_in1", "", false },
                    { 46, "pcnt_ctrl_ch1_in1", "", false },
                    { 47, "pcnt_sig_ch0_in2", "", false },
                    { 48, "pcnt_sig_ch1_in2", "", false },
                    { 49, "pcnt_ctrl_ch0_in2", "", false },
                    { 50, "pcnt_ctrl_ch1_in2", "", false },
                    { 51, "pcnt_sig_ch0_in3", "", false },
                    { 52, "pcnt_sig_ch1_in3", "", false },
                    { 53, "pcnt_ctrl_ch0_in3", "", false },
                    { 54, "pcnt_ctrl_ch1_in3", "", false },
                    { 55, "pcnt_sig_ch0_in4", "", false },
                    { 56, "pcnt_sig_ch1_in4", "", false },
                    { 57, "pcnt_ctrl_ch0_in4", "", false },
                    { 58, "pcnt_ctrl_ch1_in4", "", false },
                    { 61, "HSPICS1_in", "HSPICS1_out", false },
                    { 62, "HSPICS2_in", "HSPICS2_out", false },
                    { 63, "VSPICLK_in", "VSPICLK_out_mux", true },
                    { 64, "VSPIQ_in", "VSPIQ_out", true },
                    { 65, "VSPID_in", "VSPID_out", true },
                    { 66, "VSPIHD_in", "VSPIHD_out", true },
                    { 67, "VSPIWP_in", "VSPIWP_out", true },
                    { 68, "VSPICS0_in", "VSPICS0_out", true },
                    { 69, "VSPICS1_in", "VSPICS1_out", false },
                    { 70, "VSPICS2_in", "VSPICS2_out", false },
                    { 71, "pcnt_sig_ch0_in5", "ledc_hs_sig_out0", false },
                    { 72, "pcnt_sig_ch1_in5", "ledc_hs_sig_out1", false },
                    { 73, "pcnt_ctrl_ch0_in5", "ledc_hs_sig_out2", false },
                    { 74, "pcnt_ctrl_ch1_in5", "ledc_hs_sig_out3", false },
                    { 75, "pcnt_sig_ch0_in6", "ledc_hs_sig_out4", false },
                    { 76, "pcnt_sig_ch1_in6", "ledc_hs_sig_out5", false },
                    { 77, "pcnt_ctrl_ch0_in6", "ledc_hs_sig_out6", false },
                    { 78, "pcnt_ctrl_ch1_in6", "ledc_hs_sig_out7", false },
                    { 79, "pcnt_sig_ch0_in7", "ledc_ls_sig_out0", false },
                    { 80, "pcnt_sig_ch1_in7", "ledc_ls_sig_out1", false },
                    { 81, "pcnt_ctrl_ch0_in7", "ledc_ls_sig_out2", false },
                    { 82, "pcnt_ctrl_ch1_in7", "ledc_ls_sig_out3", false },
                    { 83, "rmt_sig_in0", "ledc_ls_sig_out4", false },
                    { 84, "rmt_sig_in1", "ledc_ls_sig_out5", false },
                    { 85, "rmt_sig_in2", "ledc_ls_sig_out6", false },
                    { 86, "rmt_sig_in3", "ledc_ls_sig_out7", false },
                    { 87, "rmt_sig_in4", "rmtt_sig_out0", false },
                    { 88, "rmt_sig_in5", "rmtt_sig_out1", false },
                    { 89, "rmt_sig_in6", "rmtt_sig_out2", false },
                    { 90, "rmt_sig_in7", "rmtt_sig_out3", false },
                    { 91, "", "rmtt_sig_out4", false },
                    { 92, "", "rmtt_sig_out5", false },
                    { 93, "", "rmtt_sig_out6", false },
                    { 94, "", "rmtt_sig_out7", false },
                    { 95, "I2CEXT1_SCL_in", "I2CEXT1_SCL_out", false },
                    { 96, "I2CEXT1_SDA_in", "I2CEXT1_SDA_out", false },
                    { 97, "host_card_detect_n_1", "host_ccmd_od_pullup_en_n", false },
                    { 98, "host_card_detect_n_2", "host_rst_n_1", false },
                    { 99, "host_card_write_prt_1", "host_rst_n_2", false },
                    { 100, "host_card_write_prt_2", "gpio_sd0_out", false },
                    { 101, "host_card_int_n_1", "gpio_sd1_out", false },
                    { 102, "host_card_int_n_2", "gpio_sd2_out", false },
                    { 103, "pwm1_sync0_in", "gpio_sd3_out", false },
                    { 104, "pwm1_sync1_in", "gpio_sd4_out", false },
                    { 105, "pwm1_sync2_in", "gpio_sd5_out", false },
                    { 106, "pwm1_f0_in", "gpio_sd6_out", false },
                    { 107, "pwm1_f1_in", "gpio_sd7_out", false },
                    { 108, "pwm1_f2_in", "pwm1_out0a", false },
                    { 109, "pwm0_cap0_in", "pwm1_out0b", false },
                    { 110, "pwm0_cap1_in", "pwm1_out1a", false },
                    { 111, "pwm0_cap2_in", "pwm1_out1b", false },
                    { 112, "pwm1_cap0_in", "pwm1_out2a", false },
                    { 113, "pwm1_cap1_in", "pwm1_out2b", false },
                    { 114, "pwm1_cap2_in", "", false },
                    { 115, "", "", false },
                    { 116, "", "", false },
                    { 117, "", "", false },
                    { 118, "", "", false },
                    { 119, "", "", false },
                    { 120, "", "", false },
                    { 121, "", "", false },
                    { 122, "", "", false },
                    { 123, "", "", false },
                    { 124, "", "", false },
                    { 140, "I2S0I_DATA_in0", "I2S0O_DATA_out0", false },
                    { 141, "I2S0I_DATA_in1", "I2S0O_DATA_out1", false },
                    { 142, "I2S0I_DATA_in2", "I2S0O_DATA_out2", false },
                    { 143, "I2S0I_DATA_in3", "I2S0O_DATA_out3", false },
                    { 144, "I2S0I_DATA_in4", "I2S0O_DATA_out4", false },
                    { 145, "I2S0I_DATA_in5", "I2S0O_DATA_out5", false },
                    { 146, "I2S0I_DATA_in6", "I2S0O_DATA_out6", false },
                    { 147, "I2S0I_DATA_in7", "I2S0O_DATA_out7", false },
                    { 148, "I2S0I_DATA_in8", "I2S0O_DATA_out8", false },
                    { 149, "I2S0I_DATA_in9", "I2S0O_DATA_out9", false },
                    { 150, "I2S0I_DATA_in10", "I2S0O_DATA_out10", false },
                    { 151, "I2S0I_DATA_in11", "I2S0O_DATA_out11", false },
                    { 152, "I2S0I_DATA_in12", "I2S0O_DATA_out12", false },
                    { 153, "I2S0I_DATA_in13", "I2S0O_DATA_out13", false },
                    { 154, "I2S0I_DATA_in14", "I2S0O_DATA_out14", false },
                    { 155, "I2S0I_DATA_in15", "I2S0O_DATA_out15", false },
                    { 156, "", "I2S0O_DATA_out16", false },
                    { 157, "", "I2S0O_DATA_out17", false },
                    { 158, "", "I2S0O_DATA_out18", false },
                    { 159, "", "I2S0O_DATA_out19", false },
                    { 160, "", "I2S0O_DATA_out20", false },
                    { 161, "", "I2S0O_DATA_out21", false },
                    { 162, "", "I2S0O_DATA_out22", false },
                    { 163, "", "I2S0O_DATA_out23", false },
                    { 164, "I2S1I_BCK_in", "I2S1I_BCK_out", false },
                    { 165, "I2S1I_WS_in", "I2S1I_WS_out", false },
                    { 166, "I2S1I_DATA_in0", "I2S1O_DATA_out0", false },
                    { 167, "I2S1I_DATA_in1", "I2S1O_DATA_out1", false },
                    { 168, "I2S1I_DATA_in2", "I2S1O_DATA_out2", false },
                    { 169, "I2S1I_DATA_in3", "I2S1O_DATA_out3", false },
                    { 170, "I2S1I_DATA_in4", "I2S1O_DATA_out4", false },
                    { 171, "I2S1I_DATA_in5", "I2S1O_DATA_out5", false },
                    { 172, "I2S1I_DATA_in6", "I2S1O_DATA_out6", false },
                    { 173, "I2S1I_DATA_in7", "I2S1O_DATA_out7", false },
                    { 174, "I2S1I_DATA_in8", "I2S1O_DATA_out8", false },
                    { 175, "I2S1I_DATA_in9", "I2S1O_DATA_out9", false },
                    { 176, "I2S1I_DATA_in10", "I2S1O_DATA_out10", false },
                    { 177, "I2S1I_DATA_in11", "I2S1O_DATA_out11", false },
                    { 178, "I2S1I_DATA_in12", "I2S1O_DATA_out12", false },
                    { 179, "I2S1I_DATA_in13", "I2S1O_DATA_out13", false },
                    { 180, "I2S1I_DATA_in14", "I2S1O_DATA_out14", false },
                    { 181, "I2S1I_DATA_in15", "I2S1O_DATA_out15", false },
                    { 182, "", "I2S1O_DATA_out16", false },
                    { 183, "", "I2S1O_DATA_out17", false },
                    { 184, "", "I2S1O_DATA_out18", false },
                    { 185, "", "I2S1O_DATA_out19", false },
                    { 186, "", "I2S1O_DATA_out20", false },
                    { 187, "", "I2S1O_DATA_out21", false },
                    { 188, "", "I2S1O_DATA_out22", false },
                    { 189, "", "I2S1O_DATA_out23", false },
                    { 190, "I2S0I_H_SYNC", "", false },
                    { 191, "I2S0I_V_SYNC", "", false },
                    { 192, "I2S0I_H_ENABLE", "", false },
                    { 193, "I2S1I_H_SYNC", "", false },
                    { 194, "I2S1I_V_SYNC", "", false },
                    { 195, "I2S1I_H_ENABLE", "", false },
                    { 196, "", "", false },
                    { 197, "", "", false },
                    { 198, "U2RXD_in", "U2TXD_out", true },
                    { 199, "U2CTS_in", "U2RTS_out", true },
                    { 200, "emac_mdc_i", "emac_mdc_o", false },
                    { 201, "emac_mdi_i", "emac_mdo_o", false },
                    { 202, "emac_crs_i", "emac_crs_o", false },
                    { 203, "emac_col_i", "emac_col_o", false },
                    { 204, "pcmfsync_in", "bt_audio0_irq", false },
                    { 205, "pcmclk_in", "bt_audio1_irq", false },
                    { 206, "pcmdin", "bt_audio2_irq", false },
                    { 207, "", "le_audio0_irq", false },
                    { 208, "", "le_audio1_irq", false },
                    { 209, "", "le_audio2_irq", false },
                    { 210, "", "cmfsync_out", false },
                    { 211, "", "cmclk_out", false },
                    { 212, "", "cmdout", false },
                    { 213, "", "le_audio_sync0_p", false },
                    { 214, "", "le_audio_sync1_p", false },
                    { 215, "", "le_audio_sync2_p", false },
                    { 224, "", "ig_in_func224", false },
                    { 225, "", "ig_in_func225", false },
                    { 226, "", "ig_in_func226", false },
                    { 227, "", "ig_in_func227", false },
                    { 228, "", "ig_in_func228", false },
                    { -1, "", "", false } };

static const char* out_sel_name(int function) {
    const gpio_matrix_t* p;
    for (p = gpio_matrix; p->num != -1; ++p) {
        if (p->num == function) {
            return p->out;
        }
    }
    return "";
}

static void show_matrix(Print& out) {
    const gpio_matrix_t* p;
    for (p = gpio_matrix; p->num != -1; ++p) {
        uint32_t in_sel = gpio_in_sel(p->num);
        if (in_sel & 0x80) {
            out << p->num << " " << p->in << " " << (in_sel & 0x3f);
            if (in_sel & 0x40) {
                out << " invert";
            }
            out << '\n';
        }
    }
}

#include <Print.h>
void gpio_dump(Print& out) {
    for (int gpio = 0; gpio < SOC_GPIO_PIN_COUNT; ++gpio) {
        gpio_num_t gpio_num = static_cast<gpio_num_t>(gpio);
        if (exists(gpio_num)) {
            out << gpio_num << " ";
            const char* function_name = pin_function_name(gpio_num, gpio_function(gpio_num));
            out << function_name;
            if (!strncmp(function_name, "GPIO", 4)) {
                if (is_output(gpio_num)) {
                    out << " O" << output_level(gpio_num);
                }
                if (is_input(gpio_num)) {
                    out << " I" << gpio_get_level(gpio_num);
                }
            }
            uint32_t out_sel = gpio_out_sel(gpio_num);
            if (out_sel != 256) {
                out << " " << out_sel_name(out_sel);
            }
            //            int func = gpio_function(gpio_num);
            //            if (func) {
            //                out << " function " << func;
            //            }
            out << '\n';
        }
    }
    out << "Input Matrix\n";
    show_matrix(out);
}
