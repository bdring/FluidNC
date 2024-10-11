// Copyright (c) 2024 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses the I2S FIFO

#include "Driver/step_engine.h"
#include "Driver/i2s_out.h"
#include "hal/i2s_hal.h"

#include <sdkconfig.h>

#include "Driver/delay_usecs.h"  // delay_us()

#include <esp_attr.h>  // IRAM_ATTR

#include <freertos/FreeRTOS.h>
// #include <freertos/queue.h>

#include <driver/periph_ctrl.h>
#include <rom/lldesc.h>
#include <soc/i2s_struct.h>
#include <soc/gpio_periph.h>
#include "Driver/fluidnc_gpio.h"

/* 16-bit mode: 1000000 usec / ((160000000 Hz) / 10 / 2) x 16 bit/pulse x 2(stereo) = 4 usec/pulse */
/* 32-bit mode: 1000000 usec / ((160000000 Hz) /  5 / 2) x 32 bit/pulse x 2(stereo) = 4 usec/pulse */
const uint32_t I2S_OUT_USEC_PER_PULSE = 2;

// The <atomic> library routines are not in IRAM so they can crash when called from FLASH
// The GCC intrinsic versions which are prefixed with __ are compiled inline
#define USE_INLINE_ATOMIC

#ifdef USE_INLINE_ATOMIC
#    define MEMORY_MODEL_FETCH __ATOMIC_RELAXED
#    define MEMORY_MODEL_STORE __ATOMIC_RELAXED
#    define ATOMIC_LOAD(var) __atomic_load_n(var, MEMORY_MODEL_FETCH)
#    define ATOMIC_STORE(var, val) __atomic_store_n(var, val, MEMORY_MODEL_STORE)
#    define ATOMIC_FETCH_AND(var, val) __atomic_fetch_and(var, val, MEMORY_MODEL_FETCH)
#    define ATOMIC_FETCH_OR(var, val) __atomic_fetch_or(var, val, MEMORY_MODEL_FETCH)
static uint32_t i2s_out_port_data = 0;
#else
#    include <atomic>
#    define ATOMIC_LOAD(var) atomic_load(var)
#    define ATOMIC_STORE(var, val) atomic_store(var, val)
#    define ATOMIC_FETCH_AND(var, val) atomic_fetch_and(var, val)
#    define ATOMIC_FETCH_OR(var, val) atomic_fetch_or(var, val)
static std::atomic<std::uint32_t> i2s_out_port_data = ATOMIC_VAR_INIT(0);

#endif

const int I2S_SAMPLE_SIZE = 4; /* 4 bytes, 32 bits per sample */

// inner lock
static portMUX_TYPE i2s_out_spinlock = portMUX_INITIALIZER_UNLOCKED;
#define I2S_OUT_ENTER_CRITICAL()                                                                                                           \
    do {                                                                                                                                   \
        if (xPortInIsrContext()) {                                                                                                         \
            portENTER_CRITICAL_ISR(&i2s_out_spinlock);                                                                                     \
        } else {                                                                                                                           \
            portENTER_CRITICAL(&i2s_out_spinlock);                                                                                         \
        }                                                                                                                                  \
    } while (0)
#define I2S_OUT_EXIT_CRITICAL()                                                                                                            \
    do {                                                                                                                                   \
        if (xPortInIsrContext()) {                                                                                                         \
            portEXIT_CRITICAL_ISR(&i2s_out_spinlock);                                                                                      \
        } else {                                                                                                                           \
            portEXIT_CRITICAL(&i2s_out_spinlock);                                                                                          \
        }                                                                                                                                  \
    } while (0)
#define I2S_OUT_ENTER_CRITICAL_ISR() portENTER_CRITICAL_ISR(&i2s_out_spinlock)
#define I2S_OUT_EXIT_CRITICAL_ISR() portEXIT_CRITICAL_ISR(&i2s_out_spinlock)

static int i2s_out_initialized = 0;

static pinnum_t i2s_out_ws_pin   = 255;
static pinnum_t i2s_out_bck_pin  = 255;
static pinnum_t i2s_out_data_pin = 255;

// outer lock
static portMUX_TYPE i2s_out_pulser_spinlock = portMUX_INITIALIZER_UNLOCKED;
#define I2S_OUT_PULSER_ENTER_CRITICAL()                                                                                                    \
    do {                                                                                                                                   \
        if (xPortInIsrContext()) {                                                                                                         \
            portENTER_CRITICAL_ISR(&i2s_out_pulser_spinlock);                                                                              \
        } else {                                                                                                                           \
            portENTER_CRITICAL(&i2s_out_pulser_spinlock);                                                                                  \
        }                                                                                                                                  \
    } while (0)
#define I2S_OUT_PULSER_EXIT_CRITICAL()                                                                                                     \
    do {                                                                                                                                   \
        if (xPortInIsrContext()) {                                                                                                         \
            portEXIT_CRITICAL_ISR(&i2s_out_pulser_spinlock);                                                                               \
        } else {                                                                                                                           \
            portEXIT_CRITICAL(&i2s_out_pulser_spinlock);                                                                                   \
        }                                                                                                                                  \
    } while (0)
#define I2S_OUT_PULSER_ENTER_CRITICAL_ISR() portENTER_CRITICAL_ISR(&i2s_out_pulser_spinlock)
#define I2S_OUT_PULSER_EXIT_CRITICAL_ISR() portEXIT_CRITICAL_ISR(&i2s_out_pulser_spinlock)

#if I2S_OUT_NUM_BITS == 16
#    define DATA_SHIFT 16
#else
#    define DATA_SHIFT 0
#endif

//
// Internal functions
//
void IRAM_ATTR i2s_out_push_fifo(int count) {
#if 0
    uint32_t portData = ATOMIC_LOAD(&i2s_out_port_data) << DATA_SHIFT;
#else
    uint32_t portData = i2s_out_port_data << DATA_SHIFT;
#endif
    for (int i = 0; i < count; i++) {
        I2S0.fifo_wr = portData;
    }
}

static inline void i2s_out_reset_tx_rx() {
    i2s_ll_tx_reset(&I2S0);
    i2s_ll_rx_reset(&I2S0);
}

static inline void i2s_out_reset_fifo_without_lock() {
    i2s_ll_tx_reset_fifo(&I2S0);
    i2s_ll_rx_reset_fifo(&I2S0);
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
        gpio_write(i2s_out_data_pin, !!(port_data & (1 << (I2S_OUT_NUM_BITS - 1 - i))));
        gpio_write(i2s_out_bck_pin, 1);
        gpio_write(i2s_out_bck_pin, 0);
    }
    gpio_write(i2s_out_ws_pin, 1);  // Latch
    return 0;
}

static int i2s_out_stop() {
    I2S_OUT_ENTER_CRITICAL();

    // stop TX module
    i2s_ll_tx_stop(&I2S0);

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

#if 0
    //clear pending interrupt
    i2s_ll_clear_intr_status(&I2S0, i2s_ll_get_intr_status(&I2S0));
#endif

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
    // reset FIFO
    i2s_out_reset_tx_rx();
    i2s_out_reset_fifo_without_lock();

    i2s_ll_tx_set_chan_mod(&I2S0, I2S_CHANNEL_FMT_ONLY_LEFT);
    i2s_ll_tx_stop_on_fifo_empty(&I2S0, true);

#if 0
    i2s_ll_clear_intr_status(&I2S0, 0xFFFFFFFF);
#endif

    i2s_ll_tx_start(&I2S0);

    // Wait for the first FIFO data to prevent the unintentional generation of 0 data
    delay_us(20);
    i2s_ll_tx_stop_on_fifo_empty(&I2S0, false);

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
    uint32_t bit = 1 << pin;
    if (val) {
        ATOMIC_FETCH_OR(&i2s_out_port_data, bit);
    } else {
        ATOMIC_FETCH_AND(&i2s_out_port_data, ~bit);
    }
}

uint8_t i2s_out_read(pinnum_t pin) {
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);
    return !!(port_data & (1 << pin));
}

//
// Initialize function (external function)
//
int i2s_out_init(i2s_out_init_t* init_param) {
    if (i2s_out_initialized) {
        // already initialized
        return -1;
    }

    ATOMIC_STORE(&i2s_out_port_data, init_param->init_val);

    // To make sure hardware is enabled before any hardware register operations.
    periph_module_reset(PERIPH_I2S0_MODULE);
    periph_module_enable(PERIPH_I2S0_MODULE);

    // Route the i2s pins to the appropriate GPIO
    i2s_out_gpio_attach(init_param->ws_pin, init_param->bck_pin, init_param->data_pin);

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
    i2s_ll_tx_stop_link(&I2S0);
    i2s_ll_tx_stop(&I2S0);

    // i2s_param_config

    // configure I2S data port interface.

    i2s_out_reset_fifo_without_lock();

    i2s_ll_enable_lcd(&I2S0, false);
    i2s_ll_enable_camera(&I2S0, false);
#ifdef SOC_I2S_SUPPORTS_PDM_TX
    i2s_ll_tx_enable_pdm(&I2S0, false);
    i2s_ll_rx_enable_pdm(&I2S0, false);
#endif

    i2s_ll_enable_dma(&I2S0, false);

    i2s_ll_tx_set_chan_mod(&I2S0, I2S_CHANNEL_FMT_ONLY_LEFT);

#if I2S_OUT_NUM_BITS == 16
    i2s_ll_tx_set_sample_bit(&I2S0, I2S_BITS_PER_SAMPLE_16BIT, I2S_BITS_PER_SAMPLE_16BIT);
    i2s_ll_rx_set_sample_bit(&I2S0, I2S_BITS_PER_SAMPLE_16BIT, I2S_BITS_PER_SAMPLE_16BIT);
#else
    i2s_ll_tx_set_sample_bit(&I2S0, I2S_BITS_PER_SAMPLE_32BIT, I2S_BITS_PER_SAMPLE_32BIT);
    i2s_ll_rx_set_sample_bit(&I2S0, I2S_BITS_PER_SAMPLE_32BIT, I2S_BITS_PER_SAMPLE_32BIT);
    i2s_ll_tx_enable_mono_mode(&I2S0, true);
    i2s_ll_rx_enable_mono_mode(&I2S0, true);
    // Data width is 32-bit. Forgetting this setting will result in a 16-bit transfer.
#endif
    //    I2S0.conf.tx_mono = 0;  // Set this bit to enable transmitter’s mono mode in PCM standard mode.

    i2s_ll_rx_set_chan_mod(&I2S0, 1);
    // i2s_ll_rx_set_chan_mod(&I2S0, I2S_CHANNEL_FMT_ALL_LEFT, false);
    //    I2S0.conf.rx_mono = 0;

    i2s_ll_enable_dma(&I2S0, false);  // FIFO is not connected to DMA
    i2s_ll_tx_stop(&I2S0);
    i2s_ll_rx_stop(&I2S0);

    i2s_ll_tx_enable_msb_right(&I2S0, true);     // Place right-channel data at the MSB in the transmit FIFO.
    i2s_ll_tx_enable_right_first(&I2S0, false);  // Send the left-channel data first

    i2s_ll_tx_set_slave_mod(&I2S0, false);  // Master
    i2s_ll_tx_force_enable_fifo_mod(&I2S0, true);
#ifdef SOC_I2S_SUPPORTS_PDM_RX
    i2s_ll_rx_enable_pdm(&I2S0, false);
#endif
#ifdef SOC_I2S_SUPPORTS_PDM_TX
    i2s_ll_tx_enable_pdm(&I2S0, false);
#endif

    // I2S_COMM_FORMAT_I2S_LSB
    i2s_ll_tx_set_ws_width(&I2S0, 0);          // PCM standard mode.
    i2s_ll_rx_set_ws_width(&I2S0, 0);          // PCM standard mode.
    i2s_ll_tx_enable_msb_shift(&I2S0, false);  // Do not use the Philips standard to avoid bit-shifting
    i2s_ll_rx_enable_msb_shift(&I2S0, false);  // Do not use the Philips standard to avoid bit-shifting

    // i2s_set_clk

    // set clock (fi2s) 160MHz / 5
#ifdef CONFIG_IDF_TARGET_ESP32
    i2s_ll_tx_clk_set_src(&I2S0, I2S_CLK_D2CLK);
#endif
    // N + b/a = 0
#if I2S_OUT_NUM_BITS == 16
    // N = 10
    uint16_t mclk_div = 10;
#else
    uint16_t mclk_div = 3;
    // N = 5
    // 5 could be changed to 2 to make I2SO pulse at 312.5 kHZ instead of 125 kHz, but doing so would
    // require some changes to deal with pulse lengths that are not an integral number of microseconds.
#endif
    i2s_ll_mclk_div_t first_div = { 2, 3, 47 };  // { N, b, a }
    i2s_ll_tx_set_clk(&I2S0, &first_div);

    volatile void* ptr = &I2S0;
    uint32_t       value;

    delay_us(20);
    value = *(uint32_t*)(ptr + 0xac);

    i2s_ll_mclk_div_t div = { 2, 32, 16 };  // b/a = 0.5
    i2s_ll_tx_set_clk(&I2S0, &div);

    value = *(uint32_t*)(ptr + 0xac);

    // Bit clock configuration bit in transmitter mode.
    // fbck = fi2s / tx_bck_div_num = (160 MHz / 5) / 2 = 16 MHz
    i2s_ll_tx_set_bck_div_num(&I2S0, 2);
    i2s_ll_rx_set_bck_div_num(&I2S0, 2);

    // Remember GPIO pin numbers
    i2s_out_ws_pin      = init_param->ws_pin;
    i2s_out_bck_pin     = init_param->bck_pin;
    i2s_out_data_pin    = init_param->data_pin;
    i2s_out_initialized = 1;

    // Start the I2S peripheral
    i2s_out_start();

    return 0;
}

static uint32_t _pulse_counts = 2;
static uint32_t _dir_delay_us;

// Convert the delays from microseconds to a number of I2S frame
static uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_us) {
    if (pulse_us < I2S_OUT_USEC_PER_PULSE) {
        pulse_us = I2S_OUT_USEC_PER_PULSE;
    }
    if (pulse_us > I2S_MAX_USEC_PER_PULSE) {
        pulse_us = I2S_MAX_USEC_PER_PULSE;
    }
    _dir_delay_us = dir_delay_us;
    _pulse_counts = (pulse_us + I2S_OUT_USEC_PER_PULSE - 1) / I2S_OUT_USEC_PER_PULSE;

    return _pulse_counts * I2S_OUT_USEC_PER_PULSE;
}

static int init_step_pin(int step_pin, int step_invert) {
    return step_pin;
}

// This modifies a memory variable that contains the desired
// pin states.  Later, that variable will be transferred to
// the I2S FIFO to change all the affected pins at once.
static IRAM_ATTR void set_dir_pin(int pin, int level) {
    i2s_out_write(pin, level);
}

uint32_t new_port_data;

static IRAM_ATTR void start_step() {
    new_port_data = i2s_out_port_data;
}

static IRAM_ATTR void set_step_pin(int pin, int level) {
    uint32_t bit = 1 << pin;
    if (level) {
        new_port_data |= bit;
    } else {
        new_port_data &= ~bit;
    }
}

// For direction changes, we push one sample to the FIFO
// and busy-wait for the delay.  If the delay is short enough,
// it might be possible to use the same multiple-sample trick
// that we use for step pulses, but the optimizaton might not
// be worthwhile since direction changes are infrequent.
static IRAM_ATTR void finish_dir() {
    i2s_out_push_fifo(1);
    delay_us(_dir_delay_us);
}

// After all the desired values have been set with set_pin(),
// push _pulse_counts copies of the memory variable to the
// I2S FIFO, thus creating a pulse of the desired length.
static IRAM_ATTR void finish_step() {
    if (new_port_data == i2s_out_port_data) {
        return;
    }
    for (int i = 0; i < _pulse_counts; i++) {
        I2S0.fifo_wr = new_port_data;
    }
    // There is no need for multiple "step off" samples since the timer will not fire
    // until the next time for a pulse.
    I2S0.fifo_wr = i2s_out_port_data;
}

static IRAM_ATTR int start_unstep() {
    return 1;
}

// Not called since start_unstep() returns 1
static IRAM_ATTR void finish_unstep() {}

static uint32_t max_pulses_per_sec() {
    return 1000000 / (2 * _pulse_counts * I2S_OUT_USEC_PER_PULSE);
}

// clang-format off
step_engine_t engine = {
    "I2S",
    init_engine,
    init_step_pin,
    set_dir_pin,
    finish_dir,
    start_step,
    set_step_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec
};

REGISTER_STEP_ENGINE(I2S, &engine);
