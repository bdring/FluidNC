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
void i2s_out_push_sample(uint32_t usec) {}
void i2s_out_push() {}
void i2s_out_delay() {}
int  i2s_out_set_passthrough() {
    return 0;
}
i2s_out_pulser_status_t i2s_out_get_pulser_status() {
    return PASSTHROUGH;
}
int i2s_out_set_stepping() {
    return 0;
}
int i2s_out_set_pulse_period(uint32_t period) {
    return 0;
}
int i2s_out_reset() {
    return 0;
}
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

//
// Configrations for DMA connected I2S
//
// One DMA buffer transfer takes about 2 ms
//   I2S_OUT_DMABUF_LEN / I2S_SAMPLE_SIZE x I2S_OUT_USEC_PER_PULSE
//   = 2000 / 4 x 4
//   = 2000us = 2ms
// If I2S_OUT_DMABUF_COUNT is 5, it will take about 10 ms for all the DMA buffer transfers to finish.
//
// Increasing I2S_OUT_DMABUF_COUNT has the effect of preventing buffer underflow,
// but on the other hand, it leads to a delay with pulse and/or non-pulse-generated I/Os.
// The number of I2S_OUT_DMABUF_COUNT should be chosen carefully.
//
// Reference information:
//   FreeRTOS task time slice = portTICK_PERIOD_MS = 1 ms (ESP32 FreeRTOS port)
//
const int I2S_SAMPLE_SIZE   = 4;                                    /* 4 bytes, 32 bits per sample */
const int DMA_SAMPLE_COUNT  = I2S_OUT_DMABUF_LEN / I2S_SAMPLE_SIZE; /* number of samples per buffer */
const int SAMPLE_SAFE_COUNT = (20 / I2S_OUT_USEC_PER_PULSE);        /* prevent buffer overrun ($0 should be less than or equal 20) */

typedef struct {
    uint32_t**   buffers;
    uint32_t*    current;
    uint32_t     rw_pos;
    lldesc_t**   desc;
    xQueueHandle queue;
} i2s_out_dma_t;

static i2s_out_dma_t o_dma;
static intr_handle_t i2s_out_isr_handle;

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

static volatile uint32_t i2s_out_pulse_period;
static uint32_t          i2s_out_remain_time_until_next_pulse;  // Time remaining until the next pulse (μsec)

static pinnum_t i2s_out_ws_pin   = 255;
static pinnum_t i2s_out_bck_pin  = 255;
static pinnum_t i2s_out_data_pin = 255;

static volatile i2s_out_pulser_status_t i2s_out_pulser_status = PASSTHROUGH;

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
static inline void gpio_matrix_out_check(pinnum_t gpio, uint32_t signal_idx, bool out_inv, bool oen_inv) {
    //if pin == 255, do not need to configure
    if (gpio != 255) {
        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
        gpio_set_direction((gpio_num_t)gpio, (gpio_mode_t)GPIO_MODE_DEF_OUTPUT);
        gpio_matrix_out(gpio, signal_idx, out_inv, oen_inv);
    }
}

static void IRAM_ATTR set_single_data(uint32_t portData) {
    // Apply port data in real-time (static I2S)
    I2S0.conf_single_data = portData << DATA_SHIFT;
}

void IRAM_ATTR i2s_out_push() {
    if (i2s_out_pulser_status == PASSTHROUGH) {
        set_single_data(ATOMIC_LOAD(&i2s_out_port_data));
    }
}

static inline void i2s_out_reset_fifo_without_lock() {
    I2S0.conf.rx_fifo_reset = 1;
    I2S0.conf.rx_fifo_reset = 0;
    I2S0.conf.tx_fifo_reset = 1;
    I2S0.conf.tx_fifo_reset = 0;
}

static int i2s_clear_dma_buffer(lldesc_t* dma_desc, uint32_t port_data) {
    uint32_t* buf = (uint32_t*)dma_desc->buf;
    for (int i = 0; i < DMA_SAMPLE_COUNT; i++) {
        buf[i] = port_data;
    }
    // Restore the buffer length.
    // The length may have been changed short when the data was filled in to prevent buffer overrun.
    dma_desc->length = I2S_OUT_DMABUF_LEN;
    return 0;
}

static int i2s_clear_o_dma_buffers(uint32_t port_data) {
    for (int buf_idx = 0; buf_idx < I2S_OUT_DMABUF_COUNT; buf_idx++) {
        // Initialize DMA descriptor
        o_dma.desc[buf_idx]->owner        = 1;
        o_dma.desc[buf_idx]->eof          = 1;  // set to 1 will trigger the interrupt
        o_dma.desc[buf_idx]->sosf         = 0;
        o_dma.desc[buf_idx]->length       = I2S_OUT_DMABUF_LEN;
        o_dma.desc[buf_idx]->size         = I2S_OUT_DMABUF_LEN;
        o_dma.desc[buf_idx]->buf          = (uint8_t*)o_dma.buffers[buf_idx];
        o_dma.desc[buf_idx]->offset       = 0;
        o_dma.desc[buf_idx]->qe.stqe_next = (lldesc_t*)((buf_idx < (I2S_OUT_DMABUF_COUNT - 1)) ? (o_dma.desc[buf_idx + 1]) : o_dma.desc[0]);
        i2s_clear_dma_buffer(o_dma.desc[buf_idx], port_data);
    }
    return 0;
}

static int i2s_out_gpio_attach(pinnum_t ws, pinnum_t bck, pinnum_t data) {
    // Route the i2s pins to the appropriate GPIO
    gpio_matrix_out_check(data, I2S0O_DATA_OUT23_IDX, 0, 0);
    gpio_matrix_out_check(bck, I2S0O_BCK_OUT_IDX, 0, 0);
    gpio_matrix_out_check(ws, I2S0O_WS_OUT_IDX, 0, 0);
    return 0;
}

const int I2S_OUT_DETACH_PORT_IDX = 0x100;

static int i2s_out_gpio_detach(pinnum_t ws, pinnum_t bck, pinnum_t data) {
    // Route the i2s pins to the appropriate GPIO
    gpio_matrix_out_check(ws, I2S_OUT_DETACH_PORT_IDX, 0, 0);
    gpio_matrix_out_check(bck, I2S_OUT_DETACH_PORT_IDX, 0, 0);
    gpio_matrix_out_check(data, I2S_OUT_DETACH_PORT_IDX, 0, 0);
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
    // Stop FIFO DMA
    I2S0.out_link.stop = 1;

    // Disconnect DMA from FIFO
    I2S0.fifo_conf.dscr_en = 0;  //Unset this bit to disable I2S DMA mode. (R/W)

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

    // reest TX/RX module
    I2S0.conf.tx_reset = 1;
    I2S0.conf.tx_reset = 0;
    I2S0.conf.rx_reset = 1;
    I2S0.conf.rx_reset = 0;

    // reset DMA
    I2S0.lc_conf.in_rst  = 1;
    I2S0.lc_conf.in_rst  = 0;
    I2S0.lc_conf.out_rst = 1;
    I2S0.lc_conf.out_rst = 0;

    I2S0.out_link.addr = (uint32_t)o_dma.desc[0];

    // reset FIFO
    i2s_out_reset_fifo_without_lock();

    // start DMA link
    if (i2s_out_pulser_status == PASSTHROUGH) {
        I2S0.conf_chan.tx_chan_mod = 3;  // 3:right+constant 4:left+constant (when tx_msb_right = 1)
        I2S0.conf_single_data      = port_data;
    } else {
        I2S0.conf_chan.tx_chan_mod = 4;  // 3:right+constant 4:left+constant (when tx_msb_right = 1)
        I2S0.conf_single_data      = 0;
    }

    I2S0.conf1.tx_stop_en = 1;  // BCK and WCK are suppressed while FIFO is empty

    // Connect DMA to FIFO
    I2S0.fifo_conf.dscr_en = 1;  // Set this bit to enable I2S DMA mode. (R/W)

    I2S0.int_clr.val    = 0xFFFFFFFF;
    I2S0.out_link.start = 1;

    I2S0.conf.tx_start = 1;
    // Wait for the first FIFO data to prevent the unintentional generation of 0 data
    ets_delay_us(20);
    I2S0.conf1.tx_stop_en = 0;  // BCK and WCK are generated regardless of the FIFO status

    I2S_OUT_EXIT_CRITICAL();

    return 0;
}

// Fill out one DMA buffer
// Call with the I2S_OUT_PULSER lock acquired.
// Note that the lock is temporarily released while calling the callback function.
static int i2s_fillout_dma_buffer(lldesc_t* dma_desc) {
    uint32_t* buf = (uint32_t*)dma_desc->buf;
    o_dma.rw_pos  = 0;
    // It reuses the oldest (just transferred) buffer with the name "current"
    // and fills the buffer for later DMA.
    if (i2s_out_pulser_status == STEPPING) {
        //
        // Fillout the buffer for pulse
        //
        // To avoid buffer overflow, all of the maximum pulse width (normaly about 10us)
        // is adjusted to be in a single buffer.
        // DMA_SAMPLE_SAFE_COUNT is referred to as the margin value.
        // Therefore, if a buffer is close to full and it is time to generate a pulse,
        // the generation of the buffer is interrupted (the buffer length is shortened slightly)
        // and the pulse generation is postponed until the next buffer is filled.
        //
        o_dma.rw_pos = 0;
        while (o_dma.rw_pos < (DMA_SAMPLE_COUNT - SAMPLE_SAFE_COUNT)) {
            // no data to read (buffer empty)
            if (i2s_out_remain_time_until_next_pulse < I2S_OUT_USEC_PER_PULSE) {
                // pulser status may change in pulse phase func, so I need to check it every time.
                if (i2s_out_pulser_status == STEPPING) {
                    // fillout future DMA buffer (tail of the DMA buffer chains)
                    uint32_t old_rw_pos = o_dma.rw_pos;
                    I2S_OUT_PULSER_EXIT_CRITICAL();  // Temporarily unlocked status lock as it may be locked in pulse callback.
                    Stepper::pulse_func();

                    I2S_OUT_PULSER_ENTER_CRITICAL();  // Lock again.
                    // Calculate pulse period.
                    i2s_out_remain_time_until_next_pulse += i2s_out_pulse_period - I2S_OUT_USEC_PER_PULSE * (o_dma.rw_pos - old_rw_pos);
                    if (i2s_out_pulser_status == WAITING) {
                        // i2s_out_set_passthrough() has called from the pulse function.
                        // It needs to go into pass-through mode.
                        // This DMA descriptor must be a tail of the chain.
                        dma_desc->qe.stqe_next = NULL;  // Cut the DMA descriptor ring. This allow us to identify the tail of the buffer.
                    } else if (i2s_out_pulser_status == PASSTHROUGH) {
                        // i2s_out_reset() has called during the execution of the pulse function.
                        // I2S has already in static mode, and buffers has cleared to zero.
                        // To prevent the pulse function from being called back,
                        // we assume that the buffer is already full.
                        i2s_out_remain_time_until_next_pulse = 0;                 // There is no need to fill the current buffer.
                        o_dma.rw_pos                         = DMA_SAMPLE_COUNT;  // The buffer is full.
                        break;
                    }
                    continue;
                }
            }
            // no pulse data in push buffer (pulse off or idle or callback is not defined)
            buf[o_dma.rw_pos++] = ATOMIC_LOAD(&i2s_out_port_data);
            if (i2s_out_remain_time_until_next_pulse >= I2S_OUT_USEC_PER_PULSE) {
                i2s_out_remain_time_until_next_pulse -= I2S_OUT_USEC_PER_PULSE;
            }
        }
        // set filled length to the DMA descriptor
        dma_desc->length = o_dma.rw_pos * I2S_SAMPLE_SIZE;
    } else if (i2s_out_pulser_status == WAITING) {
        i2s_clear_dma_buffer(dma_desc, 0);  // Essentially, no clearing is required. I'll make sure I know when I've written something.
        o_dma.rw_pos           = 0;         // If someone calls i2s_out_push_sample, make sure there is no buffer overflow
        dma_desc->qe.stqe_next = NULL;      // Cut the DMA descriptor ring. This allow us to identify the tail of the buffer.
    } else {
        // Stepper paused (passthrough state, static I2S control mode)
        // In the passthrough mode, there is no need to fill the buffer with port_data.
        i2s_clear_dma_buffer(dma_desc, 0);  // Essentially, no clearing is required. I'll make sure I know when I've written something.
        o_dma.rw_pos                         = 0;  // If someone calls i2s_out_push_sample, make sure there is no buffer overflow
        i2s_out_remain_time_until_next_pulse = 0;
    }

    return 0;
}

//
// I2S out DMA Interrupts handler
//
static void IRAM_ATTR i2s_out_intr_handler(void* arg) {
    lldesc_t*     finish_desc;
    portBASE_TYPE high_priority_task_awoken = pdFALSE;

    if (I2S0.int_st.out_eof || I2S0.int_st.out_total_eof) {
        if (I2S0.int_st.out_total_eof) {
            // This is tail of the DMA descriptors
            I2S_OUT_ENTER_CRITICAL_ISR();
            // Stop FIFO DMA
            I2S0.out_link.stop = 1;
            // Disconnect DMA from FIFO
            I2S0.fifo_conf.dscr_en = 0;  //Unset this bit to disable I2S DMA mode. (R/W)
            // Stop TX module
            I2S0.conf.tx_start = 0;
            I2S_OUT_EXIT_CRITICAL_ISR();
        }
        // Get the descriptor of the last item in the linkedlist
        finish_desc = (lldesc_t*)I2S0.out_eof_des_addr;

        // If the queue is full it's because we have an underflow,
        // more than buf_count isr without new data, remove the front buffer
        if (xQueueIsQueueFullFromISR(o_dma.queue)) {
            lldesc_t* front_desc;
            // Remove a descriptor from the DMA complete event queue
            xQueueReceiveFromISR(o_dma.queue, &front_desc, &high_priority_task_awoken);
            I2S_OUT_PULSER_ENTER_CRITICAL_ISR();
            uint32_t port_data = 0;
            if (i2s_out_pulser_status == STEPPING) {
                port_data = ATOMIC_LOAD(&i2s_out_port_data);
            }
            I2S_OUT_PULSER_EXIT_CRITICAL_ISR();
#    ifdef CONFIG_IDF_TARGET_ESP32
            // lldesc_t.buf is const for S2.  Perhaps we can get by
            // without replacing the data in the buffer since we are
            // already in an error situation.
            for (int i = 0; i < DMA_SAMPLE_COUNT; i++) {
                front_desc->buf[i] = port_data;
            }
#    endif
            front_desc->length = I2S_OUT_DMABUF_LEN;
        }

        // Send a DMA complete event to the I2S bitstreamer task with finished buffer
        xQueueSendFromISR(o_dma.queue, &finish_desc, &high_priority_task_awoken);
    }

    if (high_priority_task_awoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }

    // clear interrupt
    I2S0.int_clr.val = I2S0.int_st.val;  //clear pending interrupt
}

//
// I2S bitstream generator task
//
static void i2sOutTask(void* parameter) {
    lldesc_t* dma_desc;
    while (1) {
        // Wait a DMA complete event from I2S isr
        // (Block until a DMA transfer has complete)
        xQueueReceive(o_dma.queue, &dma_desc, portMAX_DELAY);
        o_dma.current = (uint32_t*)(dma_desc->buf);
        // It reuses the oldest (just transferred) buffer with the name "current"
        // and fills the buffer for later DMA.
        I2S_OUT_PULSER_ENTER_CRITICAL();  // Lock pulser status
        if (i2s_out_pulser_status == STEPPING) {
            //
            // Fillout the buffer for pulse
            //
            // To avoid buffer overflow, all of the maximum pulse width (normaly about 10us)
            // is adjusted to be in a single buffer.
            // DMA_SAMPLE_SAFE_COUNT is referred to as the margin value.
            // Therefore, if a buffer is close to full and it is time to generate a pulse,
            // the generation of the buffer is interrupted (the buffer length is shortened slightly)
            // and the pulse generation is postponed until the next buffer is filled.
            //
            i2s_fillout_dma_buffer(dma_desc);
            dma_desc->length = o_dma.rw_pos * I2S_SAMPLE_SIZE;
        } else if (i2s_out_pulser_status == WAITING) {
            if (dma_desc->qe.stqe_next == NULL) {
                // Tail of the DMA descriptor found
                // I2S TX module has already stopped by ISR
                i2s_out_stop();
                i2s_clear_o_dma_buffers(0);  // 0 for static I2S control mode (right ch. data is always 0)
                // You need to set the status before calling i2s_out_start()
                // because the process in i2s_out_start() is different depending on the status.
                i2s_out_pulser_status = PASSTHROUGH;
                i2s_out_start();
            } else {
                // Processing a buffer slightly ahead of the tail buffer.
                // We don't need to fill up the buffer by port_data any more.
                i2s_clear_dma_buffer(dma_desc, 0);  // Essentially, no clearing is required. I'll make sure I know when I've written something.
                o_dma.rw_pos           = 0;         // If someone calls i2s_out_push_sample, make sure there is no buffer overflow
                dma_desc->qe.stqe_next = NULL;      // Cut the DMA descriptor ring. This allow us to identify the tail of the buffer.
            }
        } else {
            // Stepper paused (passthrough state, static I2S control mode)
            // In the passthrough mode, there is no need to fill the buffer with port_data.
            i2s_clear_dma_buffer(dma_desc, 0);  // Essentially, no clearing is required. I'll make sure I know when I've written something.
            o_dma.rw_pos = 0;                   // If someone calls i2s_out_push_sample, make sure there is no buffer overflow
        }
        I2S_OUT_PULSER_EXIT_CRITICAL();  // Unlock pulser status

        static UBaseType_t uxHighWaterMark = 0;
#    ifdef DEBUG_TASK_STACK
        reportTaskStackSize(uxHighWaterMark);
#    endif
    }
}

//
// External funtions
//
void i2s_out_delay() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    if (i2s_out_pulser_status == PASSTHROUGH) {
        // Depending on the timing, it may not be reflected immediately,
        // so wait twice as long just in case.
        delay_us(I2S_OUT_USEC_PER_PULSE * 2);
    } else {
        // Just wait until the data now registered in the DMA descripter
        // is reflected in the I2S TX module via FIFO.
        // XXX perhaps just wait until I2SO.conf1.tx_start == 0
        delay_ms(I2S_OUT_DELAY_MS);
    }
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

void IRAM_ATTR i2s_out_push_sample(uint32_t usec) {
    int32_t num = usec / I2S_OUT_USEC_PER_PULSE;

    if (num > SAMPLE_SAFE_COUNT) {
        return;
    }
    // push at least one sample, even if num is zero)
    if (num == 0) {
        num = 1;
    }
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);
    do {
        o_dma.current[o_dma.rw_pos++] = port_data;
    } while (--num);
}

i2s_out_pulser_status_t i2s_out_get_pulser_status() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    i2s_out_pulser_status_t s = i2s_out_pulser_status;
    I2S_OUT_PULSER_EXIT_CRITICAL();
    return s;
}

int IRAM_ATTR i2s_out_set_passthrough() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    // Triggers a change of mode if it is compiled to use I2S stream.
    // The mode is not changed directly by this function.
    // Pull the trigger
    if (i2s_out_pulser_status == STEPPING) {
        i2s_out_pulser_status = WAITING;  // Start stopping the pulser (trigger)
    }
    // It is a function that may be called via i2sOutTask().
    // (i2sOutTask() -> Stepper::pulse_func() -> Stepper::go_idle() -> Stepper_Timer_Stop() -> this function)
    // And only i2sOutTask() can change the state to PASSTHROUGH.
    // So, to change the state, you need to return to i2sOutTask() as soon as possible.
    I2S_OUT_PULSER_EXIT_CRITICAL();
    return 0;
}

int i2s_out_set_stepping() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    if (i2s_out_pulser_status == STEPPING) {
        // Re-entered (fail safe)
        I2S_OUT_PULSER_EXIT_CRITICAL();
        return 0;
    }

    if (i2s_out_pulser_status == WAITING) {
        // Wait for complete DMAs
        for (;;) {
            I2S_OUT_PULSER_EXIT_CRITICAL();
            delay_ms(I2S_OUT_DELAY_DMABUF_MS);
            I2S_OUT_PULSER_ENTER_CRITICAL();
            if (i2s_out_pulser_status == WAITING) {
                continue;
            }
            if (i2s_out_pulser_status == PASSTHROUGH) {
                // DMA completed
                break;
            }
            // Another function change the I2S state to STEPPING
            I2S_OUT_PULSER_EXIT_CRITICAL();
            return 0;
        }
        // Now, DMA completed. Fallthrough.
    }

    // Change I2S state from PASSTHROUGH to STEPPING
    i2s_out_stop();
    uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);
    i2s_clear_o_dma_buffers(port_data);

    // You need to set the status before calling i2s_out_start()
    // because the process in i2s_out_start() is different depending on the status.
    i2s_out_pulser_status = STEPPING;
    i2s_out_start();
    I2S_OUT_PULSER_EXIT_CRITICAL();
    return 0;
}

int IRAM_ATTR i2s_out_set_pulse_period(uint32_t period) {
    i2s_out_pulse_period = period;
    return 0;
}

int i2s_out_reset() {
    I2S_OUT_PULSER_ENTER_CRITICAL();
    i2s_out_stop();
    if (i2s_out_pulser_status == STEPPING) {
        uint32_t port_data = ATOMIC_LOAD(&i2s_out_port_data);
        i2s_clear_o_dma_buffers(port_data);
    } else if (i2s_out_pulser_status == WAITING) {
        i2s_clear_o_dma_buffers(0);
        i2s_out_pulser_status = PASSTHROUGH;
    }

    // You need to set the status before calling i2s_out_start()
    // because the process in i2s_out_start() is different depending on the status.
    i2s_out_start();
    I2S_OUT_PULSER_EXIT_CRITICAL();
    return 0;
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

    // Allocate the array of pointers to the buffers
    o_dma.buffers = (uint32_t**)malloc(sizeof(uint32_t*) * I2S_OUT_DMABUF_COUNT);
    if (o_dma.buffers == nullptr) {
        return -1;
    }

    // Allocate each buffer that can be used by the DMA controller
    for (int buf_idx = 0; buf_idx < I2S_OUT_DMABUF_COUNT; buf_idx++) {
        o_dma.buffers[buf_idx] = (uint32_t*)heap_caps_calloc(1, I2S_OUT_DMABUF_LEN, MALLOC_CAP_DMA);
        if (o_dma.buffers[buf_idx] == nullptr) {
            return -1;
        }
    }

    // Allocate the array of DMA descriptors
    o_dma.desc = (lldesc_t**)malloc(sizeof(lldesc_t*) * I2S_OUT_DMABUF_COUNT);
    if (o_dma.desc == nullptr) {
        return -1;
    }

    // Allocate each DMA descriptor that will be used by the DMA controller
    for (int buf_idx = 0; buf_idx < I2S_OUT_DMABUF_COUNT; buf_idx++) {
        o_dma.desc[buf_idx] = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t), MALLOC_CAP_DMA);
        if (o_dma.desc[buf_idx] == nullptr) {
            return -1;
        }
    }

    // Initialize
    i2s_clear_o_dma_buffers(init_param.init_val);
    o_dma.rw_pos  = 0;
    o_dma.current = NULL;
    o_dma.queue   = xQueueCreate(I2S_OUT_DMABUF_COUNT, sizeof(uint32_t*));

    // Set the first DMA descriptor
    I2S0.out_link.addr = (uint32_t)o_dma.desc[0];

    // stop i2s
    I2S0.out_link.stop = 1;
    I2S0.conf.tx_start = 0;

    I2S0.int_clr.val = I2S0.int_st.val;  //clear pending interrupt

    //
    // i2s_param_config
    //

    // configure I2S data port interface.

    //reset i2s
    I2S0.conf.tx_reset = 1;
    I2S0.conf.tx_reset = 0;
    I2S0.conf.rx_reset = 1;
    I2S0.conf.rx_reset = 0;

    //reset dma
    I2S0.lc_conf.in_rst  = 1;  // Set this bit to reset in DMA FSM. (R/W)
    I2S0.lc_conf.in_rst  = 0;
    I2S0.lc_conf.out_rst = 1;  // Set this bit to reset out DMA FSM. (R/W)
    I2S0.lc_conf.out_rst = 0;

    // A lot of the stuff below could probably be replaced by i2s_set_clk();

    i2s_out_reset_fifo_without_lock();

    //Enable and configure DMA
    I2S0.lc_conf.check_owner        = 0;
    I2S0.lc_conf.out_loop_test      = 0;
    I2S0.lc_conf.out_auto_wrback    = 0;  // Disable auto outlink-writeback when all the data has been transmitted
    I2S0.lc_conf.out_data_burst_en  = 0;
    I2S0.lc_conf.outdscr_burst_en   = 0;
    I2S0.lc_conf.out_no_restart_clr = 0;
    I2S0.lc_conf.indscr_burst_en    = 0;
    I2S0.lc_conf.out_eof_mode       = 1;  // I2S_OUT_EOF_INT generated when DMA has popped all data from the FIFO;
    I2S0.conf2.lcd_en               = 0;
    I2S0.conf2.camera_en            = 0;
#    ifdef SOC_I2S_SUPPORTS_PDM_TX
    // i2s_ll_tx_enable_pdm(dev, false);
    // i2s_ll_tx_enable_pdm(dev2, false);
    I2S0.pdm_conf.pcm2pdm_conv_en = 0;
    I2S0.pdm_conf.pdm2pcm_conv_en = 0;
#    endif

    I2S0.fifo_conf.dscr_en = 0;

    if (i2s_out_pulser_status == STEPPING) {
        // Channel output mode
        I2S0.conf_chan.tx_chan_mod = 4;  // 3:right+constant 4:left+constant (when tx_msb_right = 1)
        I2S0.conf_single_data      = 0;
    } else {
        // Static output mode
        I2S0.conf_chan.tx_chan_mod = 3;  // 3:right+constant 4:left+constant (when tx_msb_right = 1)
        I2S0.conf_single_data      = init_param.init_val;
    }

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

    I2S0.fifo_conf.dscr_en = 1;  //connect DMA to fifo
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

    // Enable TX interrupts (DMA Interrupts)
    I2S0.int_ena.out_eof       = 1;  // Triggered when rxlink has finished sending a packet.
    I2S0.int_ena.out_dscr_err  = 0;  // Triggered when invalid rxlink descriptors are encountered.
    I2S0.int_ena.out_total_eof = 1;  // Triggered when all transmitting linked lists are used up.
    I2S0.int_ena.out_done      = 0;  // Triggered when all transmitted and buffered data have been read.

    // default pulse callback period (μsec)
    i2s_out_pulse_period = init_param.pulse_period;

    // Create the task that will feed the buffer
    xTaskCreatePinnedToCore(i2sOutTask,
                            "I2SOutTask",
                            4096,
                            NULL,
                            3,
                            nullptr,
                            CONFIG_ARDUINO_RUNNING_CORE  // must run the task on same core
    );

    // Allocate and Enable the I2S interrupt
    esp_intr_alloc(ETS_I2S0_INTR_SOURCE, 0, i2s_out_intr_handler, nullptr, &i2s_out_isr_handle);
    esp_intr_enable(i2s_out_isr_handle);

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
