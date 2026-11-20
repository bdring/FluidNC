// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// Stepping engine that uses the ESP32-P4 PARLIO peripheral to shift a 16-bit
// word stream.  A circular GDMA ring keeps the TX unit fed, and the DMA EOF
// callback runs pulse_func at each step boundary.

#include "Platform.h"
#include "Machine/MachineConfig.h"

#include "Driver/delay_usecs.h"
#include "Driver/fluidnc_gpio.h"
#include "Driver/step_engine.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_cache.h>
#include <esp_clk_tree.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_private/gdma.h>
#include <esp_private/gdma_link.h>
#include <esp_private/periph_ctrl.h>
#include <esp_rom_gpio.h>
#include <hal/parlio_ll.h>
#include <soc/parlio_periph.h>

namespace {

constexpr size_t CHUNK_WORDS = 64;
constexpr size_t CHUNK_COUNT = 8;
constexpr size_t RING_WORDS  = CHUNK_WORDS * CHUNK_COUNT;
constexpr size_t CHUNK_BYTES = CHUNK_WORDS * sizeof(uint16_t);

gdma_channel_handle_t  dma   = nullptr;
gdma_link_list_handle_t link = nullptr;
uintptr_t              head  = 0;
uint16_t*              ring  = nullptr;
size_t                 ring_align  = 4;
bool                   dma_started = false;

uint32_t pulse_words = 4;
uint32_t dir_words   = 0;
uint32_t step_invert_mask = 0;
uint32_t step_pin_mask    = 0;

volatile uint32_t delay_words     = 1000;
volatile uint32_t next_ticks      = 1000;
volatile uint32_t dir_mask        = 0;
volatile uint32_t idle_word       = 0;
volatile uint32_t pulse_data      = 0;

static void IRAM_ATTR updateIdleWord(uint32_t value) {
    if (idle_word != value) {
        idle_word = value;
        parlio_ll_tx_set_idle_data_value(&PARL_IO, value);
    }
}
volatile uint32_t remaining_dir   = 0;
volatile uint32_t remaining_pulse = 0;
volatile uint32_t remaining_delay = 0;

volatile bool    running          = false;
volatile bool    start_pending    = false;
volatile bool    stop_requested   = false;
volatile bool    end_after_words  = false;
volatile bool    stream_done      = false;
volatile uint8_t refill_chunk     = 0;
volatile uint8_t drain_remaining  = 0;

bool (*pulse_cb)(void) = nullptr;

void resetGenerator();

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

int IRAM_ATTR bitForPin(pinnum_t pin) {
    auto bus = config->_parlio;
    if (!bus || pin >= ParlioBus::NUM_DATA_PINS || !bus->_data_defined[pin]) {
        return -1;
    }
    return (int)pin;
}

void syncChunk(uint8_t index) {
    void* p = ring + (size_t)index * CHUNK_WORDS;
    esp_cache_msync(p, CHUNK_BYTES, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void idleChunk(uint8_t index) {
    uint16_t* dst = ring + (size_t)index * CHUNK_WORDS;
    uint16_t  v   = (uint16_t)idle_word;
    for (size_t i = 0; i < CHUNK_WORDS; ++i) {
        dst[i] = v;
    }
    syncChunk(index);
}

void fillChunk(uint8_t index) {
    uint16_t* dst = ring + (size_t)index * CHUNK_WORDS;

    uint32_t idle      = idle_word;
    uint32_t pdata     = pulse_data;
    uint32_t rdir      = remaining_dir;
    uint32_t rpulse    = remaining_pulse;
    uint32_t rdelay    = remaining_delay;
    size_t   i         = 0;

    while (i < CHUNK_WORDS) {
        if (rdir) {
            dst[i++] = (uint16_t)idle;
            --rdir;
            continue;
        }
        if (rpulse) {
            dst[i++] = (uint16_t)pdata;
            --rpulse;
            continue;
        }
        if (rdelay) {
            dst[i++] = (uint16_t)idle;
            --rdelay;
            continue;
        }

        if (end_after_words || stop_requested) {
            remaining_dir   = 0;
            remaining_pulse = 0;
            remaining_delay = 0;
            while (i < CHUNK_WORDS) {
                dst[i++] = (uint16_t)idle;
            }
            stream_done     = true;
            drain_remaining = CHUNK_COUNT;
            syncChunk(index);
            return;
        }

        // pulse_func can return false after setting the final step bits.
        // Emit those bits, then stop.
        uint32_t old_idle = idle;
        pulse_data        = old_idle;
        bool more         = pulse_cb ? pulse_cb() : false;

        pdata  = pulse_data;
        idle   = idle_word;
        uint32_t delay  = delay_words;
        // Only a change on a step pin is a step.  A direction-only change
        // must not cause a pulse word to be emitted.
        bool is_step = ((pdata ^ idle) & step_pin_mask) != 0;

        rdir   = is_step ? dir_words : 0;
        rpulse = is_step ? pulse_words : 0;
        uint32_t used = rpulse + rdir;
        rdelay = (delay > used) ? (delay - used) : 0;

        if (!more) {
            end_after_words = true;
        }
    }

    pulse_data      = pdata;
    remaining_dir   = rdir;
    remaining_pulse = rpulse;
    remaining_delay = rdelay;
    updateIdleWord(idle);
    syncChunk(index);
}

bool IRAM_ATTR onDmaEof(gdma_channel_handle_t chan, gdma_event_data_t* ev, void* ctx) {
    (void)chan;
    (void)ev;
    (void)ctx;

    portENTER_CRITICAL_ISR(&mux);

    uint8_t idx   = refill_chunk;
    refill_chunk  = (uint8_t)((idx + 1) % CHUNK_COUNT);

    // A motion armed by start_timer() starts at the next chunk boundary, so
    // the direction pins hold their previous levels until the first step word.
    if (start_pending && !running) {
        start_pending  = false;
        stop_requested = false;
        resetGenerator();
        running = true;
    }

    if (!running) {
        idleChunk(idx);
    } else if (stream_done) {
        idleChunk(idx);
        if (drain_remaining > 0) {
            drain_remaining = drain_remaining - 1;
        }
        if (drain_remaining == 0) {
            running = false;
        }
    } else {
        fillChunk(idx);
    }

    portEXIT_CRITICAL_ISR(&mux);
    return false;
}

bool initParlio() {
    PERIPH_RCC_ATOMIC() {
        parlio_ll_enable_bus_clock(0, true);
        parlio_ll_reset_register(0);
    }

    PERIPH_RCC_ATOMIC() {
        parlio_ll_tx_reset_clock(&PARL_IO);
    }
    parlio_ll_tx_reset_fifo(&PARL_IO);
    PERIPH_RCC_ATOMIC() {
        parlio_ll_tx_enable_clock(&PARL_IO, false);
    }

    parlio_ll_tx_enable_clock_gating(&PARL_IO, false);
    parlio_ll_tx_set_bus_width(&PARL_IO, 16);
    parlio_ll_tx_set_bit_pack_order(&PARL_IO, PARLIO_BIT_PACK_ORDER_LSB);
    parlio_ll_tx_set_sample_clock_edge(&PARL_IO, PARLIO_SAMPLE_EDGE_POS);
    if (!parlio_ll_tx_set_valid_delay(&PARL_IO, 0, 0)) {
        log_error("PARLIO valid delay unsupported");
        return false;
    }

    uint32_t src_hz = 0;
    if (esp_clk_tree_src_get_freq_hz((soc_module_clk_t)PARLIO_CLK_SRC_PLL_F160M,
                                     ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                     &src_hz) != ESP_OK ||
        src_hz == 0) {
        src_hz = 160000000;
    }

    uint32_t integer = (src_hz + STEPPING_FREQUENCY / 2) / STEPPING_FREQUENCY;
    if (integer < 1) {
        integer = 1;
    }
    if (integer > PARLIO_LL_TX_MAX_CLK_INT_DIV) {
        log_error("PARLIO clock divider too large");
        return false;
    }

    hal_utils_clk_div_t div = {};
    div.integer            = integer;

    PERIPH_RCC_ATOMIC() {
        parlio_ll_tx_enable_clock(&PARL_IO, true);
        parlio_ll_tx_set_clock_source(&PARL_IO, PARLIO_CLK_SRC_PLL_F160M);
        parlio_ll_tx_set_clock_div(&PARL_IO, &div);
    }
    return true;
}

bool initDma() {
    gdma_channel_alloc_config_t chan_cfg = {};
    chan_cfg.direction                   = GDMA_CHANNEL_DIRECTION_TX;

    esp_err_t err = gdma_new_axi_channel(&chan_cfg, &dma);
    if (err != ESP_OK) {
        dma = nullptr;
        log_error("PARLIO GDMA channel alloc failed");
        return false;
    }

    err = gdma_connect(dma, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_PARLIO, 0));
    if (err != ESP_OK) {
        log_error("PARLIO GDMA connect failed");
        return false;
    }

    gdma_strategy_config_t strat = {};
    strat.owner_check            = false;
    strat.auto_update_desc       = false;
    strat.eof_till_data_popped   = true;
    if (gdma_apply_strategy(dma, &strat) != ESP_OK) {
        log_error("PARLIO GDMA strategy failed");
        return false;
    }

    gdma_transfer_config_t xfer = {};
    xfer.max_data_burst_size    = 32;
    xfer.access_ext_mem         = false;
    if (gdma_config_transfer(dma, &xfer) != ESP_OK) {
        log_error("PARLIO GDMA transfer config failed");
        return false;
    }

    size_t int_align = 0;
    size_t ext_align = 0;
    if (gdma_get_alignment_constraints(dma, &int_align, &ext_align) != ESP_OK || int_align == 0) {
        log_error("PARLIO GDMA alignment query failed");
        return false;
    }
    if (int_align < 64) {
        int_align = 64;
    }
    if (CHUNK_BYTES % int_align != 0) {
        log_error("PARLIO GDMA chunk alignment mismatch");
        return false;
    }
    ring_align = int_align;

    ring = (uint16_t*)heap_caps_aligned_alloc(ring_align, RING_WORDS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ring) {
        log_error("PARLIO ring alloc failed");
        return false;
    }

    gdma_link_list_config_t link_cfg = {};
    link_cfg.num_items               = CHUNK_COUNT;
    link_cfg.item_alignment          = 8;
    link_cfg.flags.items_in_ext_mem  = 0;
    link_cfg.flags.check_owner       = 0;
    if (gdma_new_link_list(&link_cfg, &link) != ESP_OK) {
        link = nullptr;
        log_error("PARLIO GDMA link alloc failed");
        return false;
    }

    gdma_buffer_mount_config_t mounts[CHUNK_COUNT] = {};
    for (size_t i = 0; i < CHUNK_COUNT; ++i) {
        mounts[i].buffer           = ring + i * CHUNK_WORDS;
        mounts[i].buffer_alignment = ring_align;
        mounts[i].length           = CHUNK_BYTES;
        mounts[i].flags.mark_eof   = 1;
        mounts[i].flags.mark_final = GDMA_FINAL_LINK_TO_DEFAULT;
    }
    mounts[CHUNK_COUNT - 1].flags.mark_final = GDMA_FINAL_LINK_TO_START;
    if (gdma_link_mount_buffers(link, 0, mounts, CHUNK_COUNT, nullptr) != ESP_OK) {
        log_error("PARLIO GDMA mount failed");
        return false;
    }

    for (size_t i = 0; i < CHUNK_COUNT; ++i) {
        gdma_link_set_owner(link, (int)i, GDMA_LLI_OWNER_DMA);
    }

    head = gdma_link_get_head_addr(link);
    if (head == 0) {
        log_error("PARLIO GDMA head address failed");
        return false;
    }

    gdma_tx_event_callbacks_t cbs = {};
    cbs.on_trans_eof              = onDmaEof;
    if (gdma_register_tx_event_callbacks(dma, &cbs, nullptr) != ESP_OK) {
        log_error("PARLIO GDMA callback register failed");
        return false;
    }
    return true;
}

static gpio_drive_cap_t driveCapability(int32_t drive_ma) {
    switch (drive_ma) {
        case 5:
            return GPIO_DRIVE_CAP_0;
        case 10:
            return GPIO_DRIVE_CAP_1;
        case 20:
            return GPIO_DRIVE_CAP_2;
        case 40:
            return GPIO_DRIVE_CAP_3;
        default:
            // Should be unreachable. ParlioBus::group() accepts only 5, 10, 20 or 40.
            return GPIO_DRIVE_CAP_1;
    }
}

void routePins() {
    // Keep the idle level steady while the pads switch to PARLIO.
    parlio_ll_tx_set_idle_data_value(&PARL_IO, idle_word);

    auto bus = config->_parlio;
    auto drive = driveCapability(bus->_drive_ma);
    log_info("PARLIO drive " << bus->_drive_ma << " mA");

    pinMode(bus->_clk_native, OUTPUT);
    if (gpio_set_drive_capability((gpio_num_t)bus->_clk_native, drive) != ESP_OK) {
        log_error("PARLIO clk drive capability failed");
    }
    digitalWrite(bus->_clk_native, LOW);
    esp_rom_gpio_connect_out_signal((uint32_t)bus->_clk_native,
                                    parlio_periph_signals.groups[0].tx_units[0].clk_out_sig,
                                    false,
                                    false);

    for (int i = 0; i < ParlioBus::NUM_DATA_PINS; ++i) {
        if (!bus->_data_defined[i]) {
            continue;
        }
        int pin = bus->_data_native[i];
        pinMode(pin, OUTPUT);
        if (gpio_set_drive_capability((gpio_num_t)pin, drive) != ESP_OK) {
            log_error("PARLIO data drive capability failed for pin " << pin);
        }
        digitalWrite(pin, LOW);
        esp_rom_gpio_connect_out_signal((uint32_t)pin, parlio_periph_signals.groups[0].tx_units[0].data_sigs[i], false, false);
        gpio_input_enable((gpio_num_t)pin);
    }
}

bool startHardware(uint32_t idle_level) {
    if (dma_started) {
        return true;
    }

    parlio_ll_tx_reset_fifo(&PARL_IO);
    parlio_ll_tx_set_idle_data_value(&PARL_IO, idle_level);
    parlio_ll_tx_set_eof_condition(&PARL_IO, PARLIO_LL_TX_EOF_COND_DATA_LEN);
    parlio_ll_tx_set_trans_bit_len(&PARL_IO, 0x01);

    esp_err_t err = gdma_start(dma, head);
    if (err != ESP_OK) {
        log_error("PARLIO GDMA start failed");
        return false;
    }
    dma_started = true;

    uint32_t guard = 200000;
    while (!parlio_ll_tx_is_ready(&PARL_IO) && --guard) {
    }
    if (guard == 0) {
        log_error("PARLIO FIFO not ready");
        gdma_stop(dma);
        gdma_reset(dma);
        dma_started = false;
        return false;
    }

    parlio_ll_tx_start(&PARL_IO, true);
    PERIPH_RCC_ATOMIC() {
        parlio_ll_tx_enable_clock(&PARL_IO, true);
    }
    return true;
}

void resetGenerator() {
    uint32_t min_ticks = 2 * pulse_words + dir_words;
    delay_words        = next_ticks;
    if (delay_words < min_ticks) {
        delay_words = min_ticks;
    }
    next_ticks      = delay_words;
    remaining_dir   = 0;
    remaining_pulse = 0;
    remaining_delay = 0;
    end_after_words = false;
    stream_done     = false;
    // refill_chunk tracks the DMA ring position; do not reset it here.
    drain_remaining = 0;
    idle_word       = dir_mask | step_invert_mask;
    pulse_data      = idle_word;
}

uint32_t init_engine(uint32_t dir_delay_us, uint32_t pulse_delay_us, uint32_t& frequency, bool (*fn)(void)) {
    frequency = STEPPING_FREQUENCY;

    if (!config->_parlio) {
        log_config_error("PARLIO bus is not configured");
        return 0;
    }

    pulse_words = pulse_delay_us ? pulse_delay_us : 1;
    dir_words   = dir_delay_us;
    next_ticks  = 2 * pulse_words + dir_words;
    pulse_cb    = fn;

    if (!initParlio() || !initDma()) {
        return 0;
    }
    routePins();

    idle_word  = dir_mask | step_invert_mask;
    pulse_data = idle_word;
    for (size_t i = 0; i < CHUNK_COUNT; ++i) {
        idleChunk((uint8_t)i);
    }

    if (!startHardware(idle_word)) {
        return 0;
    }
    return pulse_words;
}

uint32_t init_step_pin(pinnum_t pin, bool inverted) {
    int bit = bitForPin(pin);
    if (bit < 0) {
        log_config_error("Step pin " << pin << " is not on the PARLIO data bus");
        return INVALID_PINNUM;
    }

    if (inverted) {
        step_invert_mask |= (1UL << bit);
    } else {
        step_invert_mask &= ~(1UL << bit);
    }
    step_pin_mask |= (1UL << bit);
    idle_word = dir_mask | step_invert_mask;
    return bit;
}

void IRAM_ATTR set_dir_pin(pinnum_t pin, bool level) {
    int bit = bitForPin(pin);
    if (bit < 0) {
        return;
    }
    uint32_t mask = 1UL << bit;
    if (level) {
        __atomic_fetch_or(&dir_mask, mask, __ATOMIC_SEQ_CST);
    } else {
        __atomic_fetch_and(&dir_mask, ~mask, __ATOMIC_SEQ_CST);
    }
}

void finish_dir() {}

void IRAM_ATTR start_step() {
    uint32_t idle = dir_mask | step_invert_mask;
    updateIdleWord(idle);
    pulse_data = idle;
}

void IRAM_ATTR set_step_pin(pinnum_t id, bool level) {
    if (id < 0 || id >= 16) {
        return;
    }
    uint32_t bit = 1UL << (uint8_t)id;
    if (level) {
        pulse_data |= bit;
    } else {
        pulse_data &= ~bit;
    }
}

void finish_step() {}

bool IRAM_ATTR start_unstep() {
    return true;
}

void finish_unstep() {}

uint32_t max_pulses_per_sec() {
    uint32_t budget = 2 * pulse_words + dir_words;
    return budget ? STEPPING_FREQUENCY / budget : 0;
}

void IRAM_ATTR set_timer_ticks(uint32_t ticks) {
    uint32_t min_ticks = 2 * pulse_words + dir_words;
    if (ticks < min_ticks) {
        ticks = min_ticks;
    }
    if (ticks == 0) {
        ticks = 1;
    }
    next_ticks  = ticks;
    delay_words = ticks;
}

void start_timer() {
    // The ring streams continuously; arm the motion and let onDmaEof() start
    // it at the next chunk boundary.
    parlio_ll_tx_set_idle_data_value(&PARL_IO, idle_word);
    start_pending = true;
}

void IRAM_ATTR stop_timer() {
    stop_requested = true;
}

// clang-format off
step_engine_t parlio_engine = {
    "PARLIO",
    init_engine,
    init_step_pin,
    set_dir_pin,
    finish_dir,
    start_step,
    set_step_pin,
    finish_step,
    start_unstep,
    finish_unstep,
    max_pulses_per_sec,
    set_timer_ticks,
    start_timer,
    stop_timer,
    nullptr,
};
// clang-format on

}  // namespace

REGISTER_STEP_ENGINE(Parlio, &parlio_engine);
