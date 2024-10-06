// Copyright (c) 2018 - Simon Jouet
// Copyright (c) 2020 - Michiyasu Odaki
// Copyright (c) 2020 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "I2SOut.h"

#include <sdkconfig.h>

#ifndef CONFIG_IDF_TARGET_ESP32
// The newer ESP32 variants have quite different I2S hardware engines
// then the old ESP32 hardware.  For now we stub out I2S support for new ESP32s

uint8_t i2s_out_read(pinnum_t pin) {
    return 0;
}
void i2s_out_write(pinnum_t pin, uint8_t val) {}
void i2s_out_delay() {}

void IRAM_ATTR i2s_out_push_fifo(int count) {}

int i2s_out_init() {
    return -1;
}
#else
#    include "Config.h"
#    include "Pin.h"
#    include "Settings.h"
#    include "SettingsDefinitions.h"
#    include "Machine/MachineConfig.h"
#    include "Stepper.h"

#    include <esp_attr.h>  // IRAM_ATTR

#    include <freertos/FreeRTOS.h>
#    include <driver/periph_ctrl.h>
#    include <rom/lldesc.h>
#    include <soc/i2s_struct.h>
#    include <freertos/queue.h>
#    include <soc/gpio_periph.h>
#    include "Driver/fluidnc_gpio.h"

// The <atomic> library routines are not in IRAM so they can crash when called from FLASH
// The GCC intrinsic versions which are prefixed with __ are compiled inline
#    define USE_INLINE_ATOMIC

#    ifdef USE_INLINE_ATOMIC
#        define MEMORY_MODEL_FETCH __ATOMIC_RELAXED
#        define MEMORY_MODEL_STORE __ATOMIC_RELAXED
#        define ATOMIC_LOAD(var) __atomic_load_n(var, MEMORY_MODEL_FETCH)
#        define ATOMIC_STORE(var, val) __atomic_store_n(var, val, MEMORY_MODEL_STORE)
#        define ATOMIC_FETCH_AND(var, val) __atomic_fetch_and(var, val, MEMORY_MODEL_FETCH)
#        define ATOMIC_FETCH_OR(var, val) __atomic_fetch_or(var, val, MEMORY_MODEL_FETCH)
static uint32_t i2s_out_port_data = 0;
#    else
#        include <atomic>
#        define ATOMIC_LOAD(var) atomic_load(var)
#        define ATOMIC_STORE(var, val) atomic_store(var, val)
#        define ATOMIC_FETCH_AND(var, val) atomic_fetch_and(var, val)
#        define ATOMIC_FETCH_OR(var, val) atomic_fetch_or(var, val)
static std::atomic<std::uint32_t> i2s_out_port_data = ATOMIC_VAR_INIT(0);

#    endif

const int I2S_SAMPLE_SIZE = 4; /* 4 bytes, 32 bits per sample */

// inner lock
static portMUX_TYPE i2s_out_spinlock = portMUX_INITIALIZER_UNLOCKED;
#    define I2S_OUT_ENTER_CRITICAL()                                                                                                       \
        do {                                                                                                                               \
            if (xPortInIsrContext()) {                                                                                                     \
                portENTER_CRITICAL_ISR(&i2s_out_spinlock);                                                                                 \
            } else {                                                                                                                       \
                portENTER_CRITICAL(&i2s_out_spinlock);                                                                                     \
            }                                                                                                                              \
        } while (0)
#    define I2S_OUT_EXIT_CRITICAL()                                                                                                        \
        do {                                                                                                                               \
            if (xPortInIsrContext()) {                                                                                                     \
                portEXIT_CRITICAL_ISR(&i2s_out_spinlock);                                                                                  \
            } else {                                                                                                                       \
                portEXIT_CRITICAL(&i2s_out_spinlock);                                                                                      \
            }                                                                                                                              \
        } while (0)
#    define I2S_OUT_ENTER_CRITICAL_ISR() portENTER_CRITICAL_ISR(&i2s_out_spinlock)
#    define I2S_OUT_EXIT_CRITICAL_ISR() portEXIT_CRITICAL_ISR(&i2s_out_spinlock)

static int i2s_out_initialized = 0;

static pinnum_t i2s_out_ws_pin   = 255;
static pinnum_t i2s_out_bck_pin  = 255;
static pinnum_t i2s_out_data_pin = 255;

// outer lock
static portMUX_TYPE i2s_out_pulser_spinlock = portMUX_INITIALIZER_UNLOCKED;
#    define I2S_OUT_PULSER_ENTER_CRITICAL()                                                                                                \
        do {                                                                                                                               \
            if (xPortInIsrContext()) {                                                                                                     \
                portENTER_CRITICAL_ISR(&i2s_out_pulser_spinlock);                                                                          \
            } else {                                                                                                                       \
                portENTER_CRITICAL(&i2s_out_pulser_spinlock);                                                                              \
            }                                                                                                                              \
        } while (0)
#    define I2S_OUT_PULSER_EXIT_CRITICAL()                                                                                                 \
        do {                                                                                                                               \
            if (xPortInIsrContext()) {                                                                                                     \
                portEXIT_CRITICAL_ISR(&i2s_out_pulser_spinlock);                                                                           \
            } else {                                                                                                                       \
                portEXIT_CRITICAL(&i2s_out_pulser_spinlock);                                                                               \
            }                                                                                                                              \
        } while (0)
#    define I2S_OUT_PULSER_ENTER_CRITICAL_ISR() portENTER_CRITICAL_ISR(&i2s_out_pulser_spinlock)
#    define I2S_OUT_PULSER_EXIT_CRITICAL_ISR() portEXIT_CRITICAL_ISR(&i2s_out_pulser_spinlock)

#    if I2S_OUT_NUM_BITS == 16
#        define DATA_SHIFT 16
#    else
#        define DATA_SHIFT 0
#    endif

//
// Internal functions
//
void IRAM_ATTR i2s_out_push_fifo(int count) {
    uint32_t portData = ATOMIC_LOAD(&i2s_out_port_data) << DATA_SHIFT;
    for (int i = 0; i < count; i++) {
        I2S0.fifo_wr = portData;
    }
}

static inline void i2s_out_reset_fifo_without_lock() {
    I2S0.conf.rx_fifo_reset = 1;
    I2S0.conf.rx_fifo_reset = 0;
    I2S0.conf.tx_fifo_reset = 1;
    I2S0.conf.tx_fifo_reset = 0;
}

static int i2s_out_gpio_attach(pinnum_t ws, pinnum_t bck, pinnum_t data) {
    // Route the i2s pins to the appropriate GPIO
    gpio_route(data, I2S0O_DATA_OUT23_IDX);
    gpio_route(bck, I2S0O_BCK_OUT_IDX);
    gpio_route(ws, I2S0O_WS_OUT_IDX);
    return 0;
}

const int I2S_OUT_DETACH_PORT_IDX = 0x100;

static int i2s_out_gpio_detach(pinnum_t ws, pinnum_t bck, pinnum_t data) {
    // Route the i2s pins to the appropriate GPIO
    gpio_route(ws, I2S_OUT_DETACH_PORT_IDX);
    gpio_route(bck, I2S_OUT_DETACH_PORT_IDX);
    gpio_route(data, I2S_OUT_DETACH_PORT_IDX);
    return 0;
}

static int i2s_out_gpio_shiftout(uint32_t port_data) {
    gpio_write(i2s_out_ws_pin, 0);
    for (int i = 0; i < I2S_OUT_NUM_BITS; i++) {
        gpio_write(i2s_out_data_pin, !!(port_data & bitnum_to_mask(I2S_OUT_NUM_BITS - 1 - i)));
        gpio_write(i2s_out_bck_pin, 1);
        gpio_write(i2s_out_bck_pin, 0);
    }
    gpio_write(i2s_out_ws_pin, 1);  // Latch
    return 0;
}

static int i2s_out_stop() {
    I2S_OUT_ENTER_CRITICAL();

    // stop TX module
    I2S0.conf.tx_start = 0;

    // Force WS to LOW before detach
    // This operation prevents unintended WS edge trigger when detach
    gpio_write(i2s_out_ws_pin, 0);

    // Now, detach GPIO pin from I2S
    i2s_out_gpio_detach(i2s_out_ws_pin, i2s_out_bck_pin, i2s_out_data_pin);

    // Force BCK to LOW
    // After the TX module is stopped, BCK always seems to be in LOW.
    // However, I'm going to do it manually to ensure the BCK's LOW.
    gpio_write(i2s_out_bck_pin, 0);

    // Transmit recovery data to 74HC595
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);  // current expanded port value
    i2s_out_gpio_shiftout(port_data);

    //clear pending interrupt
    I2S0.int_clr.val = I2S0.int_st.val;

    I2S_OUT_EXIT_CRITICAL();
    return 0;
}

static int i2s_out_start() {
    if (!i2s_out_initialized) {
        return -1;
    }

    I2S_OUT_ENTER_CRITICAL();
    // Transmit recovery data to 74HC595
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);  // current expanded port value
    i2s_out_gpio_shiftout(port_data);

    // Attach I2S to specified GPIO pin
    i2s_out_gpio_attach(i2s_out_ws_pin, i2s_out_bck_pin, i2s_out_data_pin);

    // reset TX/RX module
    I2S0.conf.tx_reset = 1;
    I2S0.conf.tx_reset = 0;
    I2S0.conf.rx_reset = 1;
    I2S0.conf.rx_reset = 0;

    // reset FIFO
    i2s_out_reset_fifo_without_lock();

    I2S0.conf_chan.tx_chan_mod = 4;  // 3:right+constant 4:left+constant (when tx_msb_right = 1)
    I2S0.conf1.tx_stop_en      = 1;  // BCK and WCK are suppressed while FIFO is empty

    I2S0.int_clr.val = 0xFFFFFFFF;

    I2S0.conf.tx_start = 1;
    // Wait for the first FIFO data to prevent the unintentional generation of 0 data
    delay_us(20);
    I2S0.conf1.tx_stop_en = 0;  // BCK and WCK are generated regardless of the FIFO status

    I2S_OUT_EXIT_CRITICAL();

    return 0;
}

//
// External funtions
//
void i2s_out_delay() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    // Depending on the timing, it may not be reflected immediately,
    // so wait twice as long just in case.
    delay_us(I2S_OUT_USEC_PER_PULSE * 2);
    I2S_OUT_PULSER_EXIT_CRITICAL();
}

void IRAM_ATTR i2s_out_write(pinnum_t pin, uint8_t val) {
    uint32_t bit = bitnum_to_mask(pin);
    if (val) {
        ATOMIC_FETCH_OR(&i2s_out_port_data, bit);
    } else {
        ATOMIC_FETCH_AND(&i2s_out_port_data, ~bit);
    }
}

uint8_t i2s_out_read(pinnum_t pin) {
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);
    return (!!(port_data & bitnum_to_mask(pin)));
}

//
// Initialize function (external function)
//
int i2s_out_init(i2s_out_init_t& init_param) {
    if (i2s_out_initialized) {
        // already initialized
        return -1;
    }

    ATOMIC_STORE(&i2s_out_port_data, init_param.init_val);

    // To make sure hardware is enabled before any hardware register operations.
    periph_module_reset(PERIPH_I2S0_MODULE);
    periph_module_enable(PERIPH_I2S0_MODULE);

    // Route the i2s pins to the appropriate GPIO
    i2s_out_gpio_attach(init_param.ws_pin, init_param.bck_pin, init_param.data_pin);

    /**
   * Each i2s transfer will take
   *   fpll = PLL_D2_CLK      -- clka_en = 0
   *
   *   fi2s = fpll / N + b/a  -- N + b/a = clkm_div_num
   *   fi2s = 160MHz / 2
   *   fi2s = 80MHz
   *
   *   fbclk = fi2s / M   -- M = tx_bck_div_num
   *   fbclk = 80MHz / 2
   *   fbclk = 40MHz
   *
   *   fwclk = fbclk / 32
   *
   *   for fwclk = 250kHz(16-bit: 4µS pulse time), 125kHz(32-bit: 8μS pulse time)
   *      N = 10, b/a = 0
   *      M = 2
   *   for fwclk = 500kHz(16-bit: 2µS pulse time), 250kHz(32-bit: 4μS pulse time)
   *      N = 5, b/a = 0
   *      M = 2
   *   for fwclk = 1000kHz(16-bit: 1µS pulse time), 500kHz(32-bit: 2μS pulse time)
   *      N = 2, b/a = 2/1 (N + b/a = 2.5)
   *      M = 2
   */

    // stop i2s
    I2S0.out_link.stop = 1;
    I2S0.conf.tx_start = 0;

    //
    // i2s_param_config
    //

    // configure I2S data port interface.

    //reset i2s
    I2S0.conf.tx_reset = 1;
    I2S0.conf.tx_reset = 0;
    I2S0.conf.rx_reset = 1;
    I2S0.conf.rx_reset = 0;

    // A lot of the stuff below could probably be replaced by i2s_set_clk();

    i2s_out_reset_fifo_without_lock();

    I2S0.conf2.lcd_en    = 0;
    I2S0.conf2.camera_en = 0;
#    ifdef SOC_I2S_SUPPORTS_PDM_TX
    // i2s_ll_tx_enable_pdm(dev, false);
    // i2s_ll_tx_enable_pdm(dev2, false);
    I2S0.pdm_conf.pcm2pdm_conv_en = 0;
    I2S0.pdm_conf.pdm2pcm_conv_en = 0;
#    endif

    I2S0.fifo_conf.dscr_en = 0;

    I2S0.conf_chan.tx_chan_mod        = 4;   // 3:right+constant 4:left+constant (when tx_msb_right = 1)

#    if I2S_OUT_NUM_BITS == 16
    I2S0.fifo_conf.tx_fifo_mod        = 0;   // 0: 16-bit dual channel data, 3: 32-bit single channel data
    I2S0.fifo_conf.rx_fifo_mod        = 0;   // 0: 16-bit dual channel data, 3: 32-bit single channel data
    I2S0.sample_rate_conf.tx_bits_mod = 16;  // default is 16-bits
    I2S0.sample_rate_conf.rx_bits_mod = 16;  // default is 16-bits
#    else
    I2S0.fifo_conf.tx_fifo_mod = 3;  // 0: 16-bit dual channel data, 3: 32-bit single channel data
    I2S0.fifo_conf.rx_fifo_mod = 3;  // 0: 16-bit dual channel data, 3: 32-bit single channel data
    // Data width is 32-bit. Forgetting this setting will result in a 16-bit transfer.
    I2S0.sample_rate_conf.tx_bits_mod = 32;
    I2S0.sample_rate_conf.rx_bits_mod = 32;
#    endif
    I2S0.conf.tx_mono                 = 0;   // Set this bit to enable transmitter’s mono mode in PCM standard mode.

    I2S0.conf_chan.rx_chan_mod = 1;  // 1: right+right
    I2S0.conf.rx_mono          = 0;

    I2S0.fifo_conf.dscr_en = 0;  // FIFO is not connected to DMA
    I2S0.conf.tx_start     = 0;
    I2S0.conf.rx_start     = 0;

    I2S0.conf.tx_msb_right   = 1;  // Set this bit to place right-channel data at the MSB in the transmit FIFO.
    I2S0.conf.tx_right_first = 0;  // Setting this bit allows the right-channel data to be sent first.

    I2S0.conf.tx_slave_mod              = 0;  // Master
    I2S0.fifo_conf.tx_fifo_mod_force_en = 1;  //The bit should always be set to 1.
#    ifdef SOC_I2S_SUPPORTS_PDM_RX
    //i2s_ll_rx_enable_pdm(dev, false);
    I2S0.pdm_conf.rx_pdm_en = 0;  // Set this bit to enable receiver’s PDM mode.
#    endif
#    ifdef SOC_I2S_SUPPORTS_PDM_TX
    //i2s_ll_tx_enable_pdm(dev, false);
    I2S0.pdm_conf.tx_pdm_en = 0;  // Set this bit to enable transmitter’s PDM mode.
#    endif

    // I2S_COMM_FORMAT_I2S_LSB
    I2S0.conf.tx_short_sync = 0;  // Set this bit to enable transmitter in PCM standard mode.
    I2S0.conf.rx_short_sync = 0;  // Set this bit to enable receiver in PCM standard mode.
    I2S0.conf.tx_msb_shift  = 0;  // Do not use the Philips standard to avoid bit-shifting
    I2S0.conf.rx_msb_shift  = 0;  // Do not use the Philips standard to avoid bit-shifting

    //
    // i2s_set_clk
    //

    // set clock (fi2s) 160MHz / 5
#    ifdef CONFIG_IDF_TARGET_ESP32
    // i2s_ll_rx_clk_set_src(dev, I2S_CLK_D2CLK);
    I2S0.clkm_conf.clka_en = 0;  // Use 160 MHz PLL_D2_CLK as reference
#    endif
        // N + b/a = 0
#    if I2S_OUT_NUM_BITS == 16
    // N = 10
    I2S0.clkm_conf.clkm_div_num = 10;  // minimum value of 2, reset value of 4, max 256 (I²S clock divider’s integral value)
#    else
    // N = 5
    // 5 could be changed to 2 to make I2SO pulse at 312.5 kHZ instead of 125 kHz, but doing so would
    // require some changes to deal with pulse lengths that are not an integral number of microseconds.
    I2S0.clkm_conf.clkm_div_num = 5;  // minimum value of 2, reset value of 4, max 256 (I²S clock divider’s integral value)
#    endif
    // b/a = 0
    I2S0.clkm_conf.clkm_div_b = 0;  // 0 at reset
    I2S0.clkm_conf.clkm_div_a = 0;  // 0 at reset, what about divide by 0? (not an issue)

    // Bit clock configuration bit in transmitter mode.
    // fbck = fi2s / tx_bck_div_num = (160 MHz / 5) / 2 = 16 MHz
    I2S0.sample_rate_conf.tx_bck_div_num = 2;  // minimum value of 2 defaults to 6
    I2S0.sample_rate_conf.rx_bck_div_num = 2;

    // Remember GPIO pin numbers
    i2s_out_ws_pin      = init_param.ws_pin;
    i2s_out_bck_pin     = init_param.bck_pin;
    i2s_out_data_pin    = init_param.data_pin;
    i2s_out_initialized = 1;

    // Start the I2S peripheral
    i2s_out_start();

    return 0;
}

#    ifndef I2S_OUT_INIT_VAL
#        define I2S_OUT_INIT_VAL 0
#    endif
/*
  Initialize I2S out by default parameters.

  return -1 ... already initialized
*/
int i2s_out_init() {
    auto i2so = config->_i2so;
    if (!i2so) {
        return -1;
    }

    Pin& wsPin   = i2so->_ws;
    Pin& bckPin  = i2so->_bck;
    Pin& dataPin = i2so->_data;

    // Check capabilities:
    if (!wsPin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
        log_info("Not setting up I2SO: WS pin has incorrect capabilities");
        return -1;
    } else if (!bckPin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
        log_info("Not setting up I2SO: BCK pin has incorrect capabilities");
        return -1;
    } else if (!dataPin.capabilities().has(Pin::Capabilities::Output | Pin::Capabilities::Native)) {
        log_info("Not setting up I2SO: DATA pin has incorrect capabilities");
        return -1;
    } else {
        i2s_out_init_t default_param;
        default_param.ws_pin       = wsPin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        default_param.bck_pin      = bckPin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        default_param.data_pin     = dataPin.getNative(Pin::Capabilities::Output | Pin::Capabilities::Native);
        default_param.pulse_period = I2S_OUT_USEC_PER_PULSE;
        default_param.init_val     = I2S_OUT_INIT_VAL;

        return i2s_out_init(default_param);
    }
}
#endif
