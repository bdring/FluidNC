#include <Driver/gpio_dump.h>
#include <Driver/fluidnc_gpio.h>
#include "driver/gpio.h"
#include "hal/gpio_hal.h"
#include <vector>
#include "Protocol.h"

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
static uint8_t gpio_function(gpio_num_t gpio) {
    return REG_GET_FIELD(GPIO_PIN_MUX_REG[gpio], MCU_SEL);
}
static uint32_t gpio_out_sel(gpio_num_t gpio) {
    return REG_READ(GPIO_FUNC0_OUT_SEL_CFG_REG + (gpio * 4));
}
static uint32_t gpio_in_sel(uint32_t function) {
    return REG_READ(GPIO_FUNC0_IN_SEL_CFG_REG + (function * 4));
}

// another way to determine available gpios is the array GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT]
// which has 0 in unavailable slots, see soc/gpio_periph.{ch}
std::vector<uint32_t> avail_gpios = { 0, 1, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 39 };

// Reset states:
//  0 - IE = 0 (input disabled)
// 1 - IE = 1 (input enabled)
// 2 - IE = 1, WPD = 1 (input enabled, pull-down resistor enabled)
// 3 - IE = 1, WPU = 1 (input enabled, pull-up resistor enabled)
// 4 - OE = 1, WPU = 1 (output enabled, pull-up resistor enabled)
// 1* - If EFUSE_DIS_PAD_JTAG = 1, the pin MTCK is left floating after reset, i.e., IE = 1.
//       If EFUSE_DIS_PAD_JTAG = 0, the pin MTCK is connected to internal pull-up resistor, i.e., IE = 1, WPU = 1.

struct pin_mux {
    pinnum_t    pinnum;
    const char* pinname;
    const char* functions[5];
    uint8_t     drive_strength;
    uint8_t     reset_state;
} const pins[] = {
    // clang-format off
    // The following was copied from a table in the
    // ESP32S3 Technical Reference manual and formatted with
    // an EMACS macro - in case it needs to be dona again for
    // a different ESP32 variant.
    { 0, "GPIO0", { "GPIO0", "GPIO0", "-", "-", "-"}, 2, 3 },
    { 1, "GPIO1", { "GPIO1", "GPIO1", "-", "-", "-"}, 2, 1 },
    { 2, "GPIO2", { "GPIO2", "GPIO2", "-", "-", "-"}, 2, 1 },
    { 3, "GPIO3", { "GPIO3", "GPIO3", "-", "-", "-"}, 2, 1 },
    { 4, "GPIO4", { "GPIO4", "GPIO4", "-", "-", "-"}, 2, 0 },
    { 5, "GPIO5", { "GPIO5", "GPIO5", "-", "-", "-"}, 2, 0 },
    { 6, "GPIO6", { "GPIO6", "GPIO6", "-", "-", "-"}, 2, 0 },
    { 7, "GPIO7", { "GPIO7", "GPIO7", "-", "-", "-"}, 2, 0 },
    { 8, "GPIO8", { "GPIO8", "GPIO8", "-", "SUBSPICS1", "-"}, 2, 0 },
    { 9, "GPIO9", { "GPIO9", "GPIO9", "-", "SUBSPIHD", "FSPIHD"}, 2, 1 },
    { 10, "GPIO10", { "GPIO10", "GPIO10", "FSPIIO4", "SUBSPICS0", "FSPICS0"}, 2, 1 },
    { 11, "GPIO11", { "GPIO11", "GPIO11", "FSPIIO5", "SUBSPID", "FSPID"}, 2, 1 },
    { 12, "GPIO12", { "GPIO12", "GPIO12", "FSPIIO6", "SUBSPICLK", "FSPICLK"}, 2, 1 },
    { 13, "GPIO13", { "GPIO13", "GPIO13", "FSPIIO7", "SUBSPIQ", "FSPIQ"}, 2, 1 },
    { 14, "GPIO14", { "GPIO14", "GPIO14", "FSPIDQS", "SUBSPIWP", "FSPIWP"}, 2, 1 },
    { 15, "XTAL_32K_P", { "GPIO15", "GPIO15", "U0RTS", "-" "-"}, 2, 0 },
    { 16, "XTAL_32K_N", { "GPIO16", "GPIO16", "U0CTS", "-" "-"}, 2, 0 },
    { 17, "GPIO17", { "GPIO17", "GPIO17", "U1TXD", "-" "-"}, 2, 1 },
    { 18, "GPIO18", { "GPIO18", "GPIO18", "U1RXD", "CLK_OUT3" "-"}, 2, 1 },
    { 19, "GPIO19", { "GPIO19", "GPIO19", "U1RTS", "CLK_OUT2" "-"}, 3, 0 },
    { 20, "GPIO20", { "GPIO20", "GPIO20", "U1CTS", "CLK_OUT1" "-"}, 3, 0 },
    { 21, "GPIO21", { "GPIO21", "GPIO21", "-", "-" "-"}, 2, 0 },
    { 26, "SPICS1", { "SPICS1", "GPIO26", "-", "-" "-"}, 2, 3 },
    { 27, "SPIHD", { "SPIHD", "GPIO27", "-", "-" "-"}, 2, 3 },
    { 28, "SPIWP", { "SPIWP", "GPIO28", "-", "-" "-"}, 2, 3 },
    { 29, "SPICS0", { "SPICS0", "GPIO29", "-", "-" "-"}, 2, 3 },
    { 30, "SPICLK", { "SPICLK", "GPIO30", "-", "-" "-"}, 2, 3 },
    { 31, "SPIQ", { "SPIQ", "GPIO31", "-", "-" "-"}, 2, 3 },
    { 32, "SPID", { "SPID", "GPIO32", "-", "-" "-"}, 2, 3 },
    { 33, "GPIO33", { "GPIO33", "GPIO33", "FSPIHD", "SUBSPIHD" "SPIIO4"}, 2, 1 },
    { 34, "GPIO34", { "GPIO34", "GPIO34", "FSPICS0", "SUBSPICS0" "SPIIO5"}, 2, 1 },
    { 35, "GPIO35", { "GPIO35", "GPIO35", "FSPID", "SUBSPID" "SPIIO6"}, 2, 1 },
    { 36, "GPIO36", { "GPIO36", "GPIO36", "FSPICLK", "SUBSPICLK" "SPIIO7"}, 2, 1 },
    { 37, "GPIO37", { "GPIO37", "GPIO37", "FSPIQ", "SUBSPIQ" "SPIDQS"}, 2, 1 },
    { 38, "GPIO38", { "GPIO38", "GPIO38", "FSPIWP", "SUBSPIWP" "-"}, 2, 1 },
    { 39, "MTCK", { "MTCK", "GPIO39", "CLK_OUT3", "SUBSPICS1" "-"}, 2, 1 },
    { 40, "MTDO", { "MTDO", "GPIO40", "CLK_OUT2", "-" "-"}, 2, 1 },
    { 41, "MTDI", { "MTDI", "GPIO41", "CLK_OUT1", "-" "-"}, 2, 1 },
    { 42, "MTMS", { "MTMS", "GPIO42", "-", "-" "-"}, 2, 1 },
    { 43, "U0TXD", { "U0TXD", "GPIO43", "CLK_OUT1", "-" "-"}, 2, 4 },
    { 44, "U0RXD", { "U0RXD", "GPIO44", "CLK_OUT2", "-" "-"}, 2, 3 },
    { 45, "GPIO45", { "GPIO45", "GPIO45", "-", "-" "-"}, 2, 2 },
    { 46, "GPIO46", { "GPIO46", "GPIO46", "-", "-" "-"}, 2, 2 },
    { 47, "SPICLK_P", { "SPICLK_P_DIFF", "GPIO47", "SUBSPICLK_P_DIFF", "-" "-"}, 2, 1 },
    { 48, "SPICLK_N", { "SPICLK_N_DIFF", "GPIO48", "SUBSPICLK_N_DIFF", "-" "-"}, 2, 1 },
    { INVALID_PINNUM, "", { "" }, 0, 0 },
};
// clang-format on

const char* pin_function_name(gpio_num_t gpio, uint8_t function) {
    const pin_mux* p;
    for (p = pins; p->pinnum != -1; ++p) {
        if (p->pinnum == gpio) {
            return p->functions[function];
        }
    }
    return "";
}

// clang-format off
#define INVALID_MATRIX_NUM 255
struct gpio_matrix_t {
    uint8_t     num;
    const char* in;
    const char* out;
    bool        iomux_in;
    bool        iomux_out;
} const gpio_matrix[] = {
{ 0, "SPIQ_IN", "SPIQ_OUT", true, true },
{ 1, "SPID_IN", "SPID_OUT", true, true },
{ 2, "SPIHD_IN", "SPIHD_OUT", true, true },
{ 3, "SPIWP_IN", "SPIWP_OUT", true, true },
{ 4, "", "SPICLK_OUT", true, true },
{ 5, "", "SPICS0_OUT", true, true },
{ 6, "", "SPICS1_OUT", true, true },
{ 7, "SPID4_IN", "SPID4_OUT", true, true },
{ 8, "SPID5_IN", "SPID5_OUT", true, true },
{ 9, "SPID6_IN", "SPID6_OUT", true, true },
{ 10, "SPID7_IN", "SPID7_OUT", true, true },
{ 11, "SPIDQS_IN", "SPIDQS_OUT", true, true },
{ 12, "U0RXD_IN", "U0TXD_OUT", true, true },
{ 13, "U0CTS_IN", "U0RTS_OUT", true, true },
{ 14, "U0DSR_IN", "U0DTR_OUT", false, false },
{ 15, "U1RXD_IN", "U1TXD_OUT", true, true },
{ 16, "U1CTS_IN", "U1RTS_OUT", true, true },
{ 17, "U1DSR_IN", "U1DTR_OUT", false, false },
{ 18, "U2RXD_IN", "U2TXD_OUT", false, false },
{ 19, "U2CTS_IN", "U2RTS_OUT", false, false },
{ 20, "U2DSR_IN", "U2DTR_OUT", false, false },
{ 21, "I2S1_MCLK_IN", "I2S1_MCLK_OUT", false, false },
{ 22, "I2S0O_BCK_IN", "I2S0O_BCK_OUT", false, false },
{ 23, "I2S0_MCLK_IN", "I2S0_MCLK_OUT", false, false },
{ 24, "I2S0O_WS_IN", "I2S0O_WS_OUT", false, false },
{ 25, "I2S0I_SD_IN", "I2S0O_SD_OUT", false, false },
{ 26, "I2S0I_BCK_IN", "I2S0I_BCK_OUT", false, false },
{ 27, "I2S0I_WS_IN", "I2S0I_WS_OUT", false, false },
{ 28, "I2S1O_BCK_IN", "I2S1O_BCK_OUT", false, false },
{ 29, "I2S1O_WS_IN", "I2S1O_WS_OUT", false, false },
{ 30, "I2S1I_SD_IN", "I2S1O_SD_OUT", false, false },
{ 31, "I2S1I_BCK_IN", "I2S1I_BCK_OUT", false, false },
{ 32, "I2S1I_WS_IN", "I2S1I_WS_OUT", false, false },
{ 33, "PCNT_SIG_CH0_IN0", "GPIO_WLAN_PRIO", false, false },
{ 34, "PCNT_SIG_CH1_IN0", "GPIO_WLAN_ACTIVE", false, false },
{ 35, "PCNT_CTRL_CH0_IN0", "BB_DIAG0", false, false },
{ 36, "PCNT_CTRL_CH1_IN0", "BB_DIAG1", false, false },
{ 37, "PCNT_SIG_CH0_IN1", "BB_DIAG2", false, false },
{ 38, "PCNT_SIG_CH1_IN1", "BB_DIAG3", false, false },
{ 39, "PCNT_CTRL_CH0_IN1", "BB_DIAG4", false, false },
{ 40, "PCNT_CTRL_CH1_IN1", "BB_DIAG5", false, false },
{ 41, "PCNT_SIG_CH0_IN2", "BB_DIAG6", false, false },
{ 42, "PCNT_SIG_CH1_IN2", "BB_DIAG7", false, false },
{ 43, "PCNT_CTRL_CH0_IN2", "BB_DIAG8", false, false },
{ 44, "PCNT_CTRL_CH1_IN2", "BB_DIAG9", false, false },
{ 45, "PCNT_SIG_CH0_IN3", "BB_DIAG10", false, false },
{ 46, "PCNT_SIG_CH1_IN3", "BB_DIAG11", false, false },
{ 47, "PCNT_CTRL_CH0_IN3", "BB_DIAG12", false, false },
{ 48, "PCNT_CTRL_CH1_IN3", "BB_DIAG13", false, false },
{ 49, "GPIO_BT_ACTIVE", "BB_DIAG14", false, false },
{ 50, "GPIO_BT_PRIORITY", "BB_DIAG15", false, false },
{ 51, "I2S0I_SD1_IN", "BB_DIAG16", false, false },
{ 52, "I2S0I_SD2_IN", "BB_DIAG17", false, false },
{ 53, "I2S0I_SD3_IN", "BB_DIAG18", false, false },
{ 54, "CORE1_GPIO_IN7", "CORE1_GPIO_OUT7", false, false },
{ 55, "USB_EXTPHY_VP", "USB_EXTPHY_OEN", false, false },
{ 56, "USB_EXTPHY_VM", "USB_EXTPHY_SPEED", false, false },
{ 57, "USB_EXTPHY_RCV", "USB_EXTPHY_VPO", false, false },
{ 58, "USB_OTG_IDDIG_IN", "USB_EXTPHY_VMO", false, false },
{ 59, "USB_OTG_AVALID_IN", "USB_EXTPHY_SUSPND", false, false },
{ 60, "USB_SRP_BVALID_IN", "USB_OTG_IDPULLUP", false, false },
{ 61, "USB_OTG_VBUSVALID_IN", "USB_OTG_DPPULLDOWN", false, false },
{ 62, "USB_SRP_SESSEND_IN", "USB_OTG_DMPULLDOWN", false, false },
{ 63, "USB_OTG_DRVVBUS", "USB_SRP_CHRGVBUS", false, false },
{ 65, "USB_SRP_DISCHRGVBUS", "SPI3_CLK_IN", false, false },
{ 66, "", "SPI3_CLK_OUT", false, false },
{ 67, "SPI3_Q_IN", "SPI3_Q_OUT", false, false },
{ 68, "SPI3_D_IN", "SPI3_D_OUT", false, false },
{ 69, "SPI3_HD_IN", "SPI3_HD_OUT", false, false },
{ 70, "SPI3_WP_IN", "SPI3_WP_OUT", false, false },
{ 71, "SPI3_CS0_IN", "SPI3_CS0_OUT", false, false },
{ 72, "", "SPI3_CS1_OUT", false, false },
{ 73, "EXT_ADC_START", "LEDC_LS_SIG_OUT0", false, false },
{ 74, "", "LEDC_LS_SIG_OUT1", false, false },
{ 75, "", "LEDC_LS_SIG_OUT2", false, false },
{ 76, "", "LEDC_LS_SIG_OUT3", false, false },
{ 77, "", "LEDC_LS_SIG_OUT4", false, false },
{ 78, "", "LEDC_LS_SIG_OUT5", false, false },
{ 79, "", "LEDC_LS_SIG_OUT6", false, false },
{ 80, "", "LEDC_LS_SIG_OUT7", false, false },
{ 81, "RMT_SIG_IN0", "RMT_SIG_OUT0", false, false },
{ 82, "RMT_SIG_IN1", "RMT_SIG_OUT1", false, false },
{ 83, "RMT_SIG_IN2", "RMT_SIG_OUT2", false, false },
{ 84, "RMT_SIG_IN3", "RMT_SIG_OUT3", false, false },
{ 85, "", "USB_JTAG_TCK", false, false },
{ 86, "", "USB_JTAG_TMS", false, false },
{ 87, "", "USB_JTAG_TDI", false, false },
{ 88, "", "USB_JTAG_TDO", false, false },
{ 89, "I2CEXT0_SCL_IN", "I2CEXT0_SCL_OUT", false, false },
{ 90, "I2CEXT0_SDA_IN", "I2CEXT0_SDA_OUT", false, false },
{ 91, "I2CEXT1_SCL_IN", "I2CEXT1_SCL_OUT", false, false },
{ 92, "I2CEXT1_SDA_IN", "I2CEXT1_SDA_OUT", false, false },
{ 93, "", "GPIO_SD0_OUT", false, false },
{ 94, "", "GPIO_SD1_OUT", false, false },
{ 95, "", "GPIO_SD2_OUT", false, false },
{ 96, "", "GPIO_SD3_OUT", false, false },
{ 97, "", "GPIO_SD4_OUT", false, false },
{ 98, "", "GPIO_SD5_OUT", false, false },
{ 99, "", "GPIO_SD6_OUT", false, false },
{ 100, "", "GPIO_SD7_OUT", false, false },
{ 101, "FSPICLK_IN", "FSPICLK_OUT", true, true },
{ 102, "FSPIQ_IN", "FSPIQ_OUT", true, true },
{ 103, "FSPID_IN", "FSPID_OUT", true, true },
{ 104, "FSPIHD_IN", "FSPIHD_OUT", true, true },
{ 105, "FSPIWP_IN", "FSPIWP_OUT", true, true },
{ 106, "FSPIIO4_IN", "FSPIIO4_OUT", true, true },
{ 107, "FSPIIO5_IN", "FSPIIO5_OUT", true, true },
{ 108, "FSPIIO6_IN", "FSPIIO6_OUT", true, true },
{ 109, "FSPIIO7_IN", "FSPIIO7_OUT", true, true },
{ 110, "FSPICS0_IN", "FSPICS0_OUT", true, true },
{ 111, "", "FSPICS1_OUT", false, false },
{ 112, "", "FSPICS2_OUT", false, false },
{ 113, "", "FSPICS3_OUT", false, false },
{ 114, "", "FSPICS4_OUT", false, false },
{ 115, "", "FSPICS5_OUT", false, false },
{ 116, "TWAI_RX", "TWAI_TX", false, false },
{ 117, "", "TWAI_BUS_OFF_ON", false, false },
{ 118, "", "TWAI_CLKOUT", false, false },
{ 119, "", "SUBSPICLK_OUT", false, false },
{ 120, "SUBSPIQ_IN", "SUBSPIQ_OUT", true, true },
{ 121, "SUBSPID_IN", "SUBSPID_OUT", true, true },
{ 122, "SUBSPIHD_IN", "SUBSPIHD_OUT", true, true },
{ 123, "SUBSPIWP_IN", "SUBSPIWP_OUT", true, true },
{ 124, "", "SUBSPICS0_OUT", true, true },
{ 125, "", "SUBSPICS1_OUT", true, true },
{ 126, "", "FSPIDQS_OUT", true, true },
{ 127, "", "SPI3_CS2_OUT", false, false },
{ 128, "", "I2S0O_SD1_OUT", false, false },
{ 129, "CORE1_GPIO_IN0", "CORE1_GPIO_OUT0", false, false },
{ 130, "CORE1_GPIO_IN1", "CORE1_GPIO_OUT1", false, false },
{ 131, "CORE1_GPIO_IN2", "CORE1_GPIO_OUT2", false, false },
{ 132, "", "LCD_CS", false, false },
{ 133, "CAM_DATA_IN0", "LCD_DATA_OUT0", false, false },
{ 134, "CAM_DATA_IN1", "LCD_DATA_OUT1", false, false },
{ 135, "CAM_DATA_IN2", "LCD_DATA_OUT2", false, false },
{ 136, "CAM_DATA_IN3", "LCD_DATA_OUT3", false, false },
{ 137, "CAM_DATA_IN4", "LCD_DATA_OUT4", false, false },
{ 138, "CAM_DATA_IN5", "LCD_DATA_OUT5", false, false },
{ 139, "CAM_DATA_IN6", "LCD_DATA_OUT6", false, false },
{ 140, "CAM_DATA_IN7", "LCD_DATA_OUT7", false, false },
{ 141, "CAM_DATA_IN8", "LCD_DATA_OUT8", false, false },
{ 142, "CAM_DATA_IN9", "LCD_DATA_OUT9", false, false },
{ 143, "CAM_DATA_IN10", "LCD_DATA_OUT10", false, false },
{ 144, "CAM_DATA_IN11", "LCD_DATA_OUT11", false, false },
{ 145, "CAM_DATA_IN12", "LCD_DATA_OUT12", false, false },
{ 146, "CAM_DATA_IN13", "LCD_DATA_OUT13", false, false },
{ 147, "CAM_DATA_IN14", "LCD_DATA_OUT14", false, false },
{ 148, "CAM_DATA_IN15", "LCD_DATA_OUT15", false, false },
{ 149, "CAM_PCLK", "CAM_CLK", false, false },
{ 150, "CAM_H_ENABLE", "LCD_H_ENABLE", false, false },
{ 151, "CAM_H_SYNC", "LCD_H_SYNC", false, false },
{ 152, "CAM_V_SYNC", "LCD_V_SYNC", false, false },
{ 153, "", "LCD_DC", false, false },
{ 154, "", "LCD_PCLK", false, false },
{ 155, "SUBSPID4_IN", "SUBSPID4_OUT", true, false },
{ 156, "SUBSPID5_IN", "SUBSPID5_OUT", true, false },
{ 157, "SUBSPID6_IN", "SUBSPID6_OUT", true, false },
{ 158, "SUBSPID7_IN", "SUBSPID7_OUT", true, false },
{ 159, "SUBSPIDQS_IN", "SUBSPIDQS_OUT", true, false },
{ 160, "PWM0_SYNC0_IN", "PWM0_OUT0A", false, false },
{ 161, "PWM0_SYNC1_IN", "PWM0_OUT0B", false, false },
{ 162, "PWM0_SYNC2_IN", "PWM0_OUT1A", false, false },
{ 163, "PWM0_F0_IN", "PWM0_OUT1B", false, false },
{ 164, "PWM0_F1_IN", "PWM0_OUT2A", false, false },
{ 165, "PWM0_F2_IN", "PWM0_OUT2B", false, false },
{ 166, "PWM0_CAP0_IN", "PWM1_OUT0A", false, false },
{ 167, "PWM0_CAP1_IN", "PWM1_OUT0B", false, false },
{ 168, "PWM0_CAP2_IN", "PWM1_OUT1A", false, false },
{ 169, "PWM1_SYNC0_IN", "PWM1_OUT1B", false, false },
{ 170, "PWM1_SYNC1_IN", "PWM1_OUT2A", false, false },
{ 171, "PWM1_SYNC2_IN", "PWM1_OUT2B", false, false },
{ 172, "PWM1_F0_IN", "SDHOST_CCLK_OUT_1", false, false },
{ 173, "PWM1_F1_IN", "SDHOST_CCLK_OUT_2", false, false },
{ 174, "PWM1_F2_IN", "SDHOST_RST_N_1", false, false },
{ 175, "PWM1_CAP0_IN", "SDHOST_RST_N_2", false, false },
{ 176, "PWM1_CAP1_IN", "SDHOST_CCMD_OD_PULLUP_EN_N", false, false },
{ 177, "PWM1_CAP2_IN", "SDIO_TOHOST_INT_OUT", false, false },
{ 178, "SDHOST_CCMD_IN_1", "SDHOST_CCMD_OUT_1", false, false },
{ 179, "SDHOST_CCMD_IN_2", "SDHOST_CCMD_OUT_2", false, false },
{ 180, "SDHOST_CDATA_IN_10", "SDHOST_CDATA_OUT_10", false, false },
{ 181, "SDHOST_CDATA_IN_11", "SDHOST_CDATA_OUT_11", false, false },
{ 182, "SDHOST_CDATA_IN_12", "SDHOST_CDATA_OUT_12", false, false },
{ 183, "SDHOST_CDATA_IN_13", "SDHOST_CDATA_OUT_13", false, false },
{ 184, "SDHOST_CDATA_IN_14", "SDHOST_CDATA_OUT_14", false, false },
{ 185, "SDHOST_CDATA_IN_15", "SDHOST_CDATA_OUT_15", false, false },
{ 186, "SDHOST_CDATA_IN_16", "SDHOST_CDATA_OUT_16", false, false },
{ 187, "SDHOST_CDATA_IN_17", "SDHOST_CDATA_OUT_17", false, false },
{ 188, "PCMFSYNC_IN", "BT_AUDIO0_IRQ", false, false },
{ 189, "PCMCLK_IN", "BT_AUDIO1_IRQ", false, false },
{ 190, "PCMDIN", "BT_AUDIO2_IRQ", false, false },
{ 191, "RW_WAKEUP_REQ", "BLE_AUDIO0_IRQ", false, false },
{ 192, "SDHOST_DATA_STROBE_1", "BLE_AUDIO1_IRQ", false, false },
{ 193, "SDHOST_DATA_STROBE_2", "BLE_AUDIO2_IRQ", false, false },
{ 194, "SDHOST_CARD_DETECT_N_1", "PCMFSYNC_OUT", false, false },
{ 195, "SDHOST_CARD_DETECT_N_2", "PCMCLK_OUT", false, false },
{ 196, "SDHOST_CARD_WRITE_PRT_1", "PCMDOUT", false, false },
{ 197, "SDHOST_CARD_WRITE_PRT_2", "BLE_AUDIO_SYNC0_P", false, false },
{ 198, "SDHOST_CARD_INT_N_1", "BLE_AUDIO_SYNC1_P", false, false },
{ 199, "SDHOST_CARD_INT_N_2", "BLE_AUDIO_SYNC2_P", false, false },
{ 200, "", "ANT_SEL0", false, false },
{ 201, "", "ANT_SEL1", false, false },
{ 202, "", "ANT_SEL2", false, false },
{ 203, "", "ANT_SEL3", false, false },
{ 204, "", "ANT_SEL4", false, false },
{ 205, "", "ANT_SEL5", false, false },
{ 206, "", "ANT_SEL6", false, false },
{ 207, "", "ANT_SEL7", false, false },
{ 208, "SIG_IN_FUNC_208", "SIG_IN_FUNC208", false, false },
{ 209, "SIG_IN_FUNC_209", "SIG_IN_FUNC209", false, false },
{ 210, "SIG_IN_FUNC_210", "SIG_IN_FUNC210", false, false },
{ 211, "SIG_IN_FUNC_211", "SIG_IN_FUNC211", false, false },
{ 212, "SIG_IN_FUNC_212", "SIG_IN_FUNC212", false, false },
{ 213, "SDHOST_CDATA_IN_20", "SDHOST_CDATA_OUT_20", false, false },
{ 214, "SDHOST_CDATA_IN_21", "SDHOST_CDATA_OUT_21", false, false },
{ 215, "SDHOST_CDATA_IN_22", "SDHOST_CDATA_OUT_22", false, false },
{ 216, "SDHOST_CDATA_IN_23", "SDHOST_CDATA_OUT_23", false, false },
{ 217, "SDHOST_CDATA_IN_24", "SDHOST_CDATA_OUT_24", false, false },
{ 218, "SDHOST_CDATA_IN_25", "SDHOST_CDATA_OUT_25", false, false },
{ 219, "SDHOST_CDATA_IN_26", "SDHOST_CDATA_OUT_26", false, false },
{ 220, "SDHOST_CDATA_IN_27", "SDHOST_CDATA_OUT_27", false, false },
{ 221, "PRO_ALONEGPIO_IN0", "PRO_ALONEGPIO_OUT0", false, false },
{ 222, "PRO_ALONEGPIO_IN1", "PRO_ALONEGPIO_OUT1", false, false },
{ 223, "PRO_ALONEGPIO_IN2", "PRO_ALONEGPIO_OUT2", false, false },
{ 224, "PRO_ALONEGPIO_IN3", "PRO_ALONEGPIO_OUT3", false, false },
{ 225, "PRO_ALONEGPIO_IN4", "PRO_ALONEGPIO_OUT4", false, false },
{ 226, "PRO_ALONEGPIO_IN5", "PRO_ALONEGPIO_OUT5", false, false },
{ 227, "PRO_ALONEGPIO_IN6", "PRO_ALONEGPIO_OUT6", false, false },
{ 228, "PRO_ALONEGPIO_IN7", "PRO_ALONEGPIO_OUT7", false, false },
{ 229, "", "SYNCERR", false, false },
{ 230, "", "SYNCFOUND_FLAG", false, false },
{ 231, "", "EVT_CNTL_IMMEDIATE_ABORT", false, false },
{ 232, "", "LINKLBL", false, false },
{ 233, "", "DATA_EN", false, false },
{ 234, "", "DATA", false, false },
{ 235, "", "PKT_TX_ON", false, false },
{ 236, "", "PKT_RX_ON", false, false },
{ 237, "", "RW_TX_ON", false, false },
{ 238, "", "RW_RX_ON", false, false },
{ 239, "", "EVT_REQ_P", false, false },
{ 240, "", "EVT_STOP_P", false, false },
{ 241, "", "BT_MODE_ON", false, false },
{ 242, "", "GPIO_LC_DIAG0", false, false },
{ 243, "", "GPIO_LC_DIAG1", false, false },
{ 244, "", "GPIO_LC_DIAG2", false, false },
{ 245, "", "CH ", false, false },
{ 246, "", "RX_WINDOW", false, false },
{ 247, "", "UPDATE_RX", false, false },
{ 248, "", "RX_STATUS", false, false },
{ 249, "", "CLK_GPIO", false, false },
{ 250, "", "NBT_BLE", false, false },
{ 251, "USB_JTAG_TDO_BRIDGE", "USB_JTAG_TRST", false, false },
{ 252, "CORE1_GPIO_IN3", "CORE1_GPIO_OUT3", false, false },
{ 253, "CORE1_GPIO_IN4", "CORE1_GPIO_OUT4", false, false },
{ 254, "CORE1_GPIO_IN5", "CORE1_GPIO_OUT5", false, false },
{ 255, "CORE1_GPIO_IN6", "CORE1_GPIO_OUT6", false, false },
{ INVALID_MATRIX_NUM, "", "", false, false }
};
// clang-format on

static const char* out_sel_name(uint8_t function) {
    const gpio_matrix_t* p;
    for (p = gpio_matrix; p->num != INVALID_MATRIX_NUM; ++p) {
        if (p->num == function) {
            return p->out;
        }
    }
    return "";
}

static void show_matrix(Print& out) {
    const gpio_matrix_t* p;
    for (p = gpio_matrix; p->num != INVALID_MATRIX_NUM; ++p) {
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

void gpio_dump(Print& out) {
    for (uint32_t gpio = 0; gpio < SOC_GPIO_PIN_COUNT; ++gpio) {
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
            //            uint8_t func = gpio_function(gpio_num);
            //            if (func) {
            //                out << " function " << func;
            //            }
            out << '\n';
        }
    }
    out << "Input Matrix\n";
    show_matrix(out);
}
