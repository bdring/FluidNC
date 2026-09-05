// Copyright (c) 2024 - FluidNC RP2040 Port
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

// RP2040 UART driver implementing the FluidNC UART interface
// Uses pico-sdk directly for hardware access

#include <Driver/fluidnc_uart.h>
#include <Driver/fluidnc_gpio.h>
#include "UartTypes.h"
#include "Protocol.h"

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pico/time.h"

class InputPin;

const int PINNUM_MAX                        = 30;  // RP2040 has GPIO 0-29

// ISR-driven software RX queues
constexpr uint16_t UART_RX_QUEUE_SIZE = 256;

struct UartRxInfo {
    uint8_t           queue[UART_RX_QUEUE_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    bool              events_enabled;
    InputPin*         objects[PINNUM_MAX];
    uint8_t           last;
};

UartRxInfo rx_info[2] = {
    { { 0 }, 0, 0, 0, false, { nullptr }, 0 },
    { { 0 }, 0, 0, 0, false, { nullptr }, 0 },
};
volatile bool irq_installed[2] = { false };

// Software flow control tracking
struct UartFlowControl {
    bool     enabled           = false;
    uint32_t xon_threshold    = 0;
    uint32_t xoff_threshold   = 0;
} flow_control[2];

// Map uart_num to pico-sdk uart_inst_t
static uart_inst_t* get_uart_instance(uint32_t uart_num) {
    if (uart_num == 0) {
        return uart0;
    } else if (uart_num == 1) {
        return uart1;
    }
    return nullptr;
}

static void uart_rx_isr(uart_inst_t* uart, UartRxInfo* rxq) {
    if (uart == nullptr || rxq == nullptr) {
        return;
    }

    // Clear RX-related interrupt causes
    uart_get_hw(uart)->icr = UART_UARTICR_RTIC_BITS | UART_UARTICR_RXIC_BITS;

    while (uart_is_readable(uart)) {
        uint8_t c = (uint8_t)uart_getc(uart);

        if (rxq->last) {
            pinnum_t pinnum = c % PINNUM_MAX;
            protocol_send_event_from_ISR(rxq->last == 0xc4 ? &pinInactiveEvent : &pinActiveEvent, (void*)rxq->objects[pinnum]);
            rxq->last = 0;
            continue;
        } else if (rxq->events_enabled && (c == 0xc4 || c == 0xc5)) {
            rxq->last = c;
            continue;
        }

        if (rxq->count < UART_RX_QUEUE_SIZE) {
            rxq->queue[rxq->head] = c;
            rxq->head             = (uint16_t)((rxq->head + 1u) & (UART_RX_QUEUE_SIZE - 1u));
            rxq->count++;
        }
    }
}

static void uart0_irq_handler() {
    uart_rx_isr(uart0, &rx_info[0]);
}

static void uart1_irq_handler() {
    uart_rx_isr(uart1, &rx_info[1]);
}

void uart_register_input_pin(uint32_t uart_num, pinnum_t pinnum, InputPin* object) {
    if (uart_num < 2 && pinnum < PINNUM_MAX) {
        auto rx             = &rx_info[uart_num];
        rx->events_enabled  = true;
        rx->objects[pinnum] = object;
        rx->last            = 0;
    }
}

void uart_init(uint32_t uart_num) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return;
    }

    // Initialize UART with default baudrate (will be set by uart_mode)
    // We use a standard 115200 baud as default
    uart_init(uart, 115200);

    // Enable FIFO
    uart_set_fifo_enabled(uart, true);

    // Reset software RX queue
    uint32_t irq_state = save_and_disable_interrupts();
    auto     rx        = &rx_info[uart_num];
    rx->head           = 0;
    rx->tail           = 0;
    rx->count          = 0;
    restore_interrupts(irq_state);

    // Install and enable UART RX IRQ path once
    if (!irq_installed[uart_num]) {
        if (uart_num == 0) {
            irq_set_exclusive_handler(UART0_IRQ, uart0_irq_handler);
            irq_set_enabled(UART0_IRQ, true);
        } else {
            irq_set_exclusive_handler(UART1_IRQ, uart1_irq_handler);
            irq_set_enabled(UART1_IRQ, true);
        }
        irq_installed[uart_num] = true;
    }
    uart_set_irq_enables(uart, true, false);
}

void uart_mode(uint32_t uart_num, uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return;
    }

    // Set baud rate
    uart_set_baudrate(uart, baud);

    // Convert UartData enum to pico-sdk data bits value (5-8)
    uint32_t data_bits = static_cast<uint32_t>(dataBits);

    // Convert UartParity enum to pico-sdk parity value
    uart_parity_t parity_mode;
    switch (parity) {
        case UartParity::None:
            parity_mode = UART_PARITY_NONE;
            break;
        case UartParity::Even:
            parity_mode = UART_PARITY_EVEN;
            break;
        case UartParity::Odd:
            parity_mode = UART_PARITY_ODD;
            break;
        default:
            parity_mode = UART_PARITY_NONE;
    }

    // Convert UartStop enum to pico-sdk stop bits value
    // Note: pico-sdk uses 1 or 2 stop bits
    uint32_t stop_bits;
    switch (stopBits) {
        case UartStop::Bits1:
            stop_bits = 1;
            break;
        case UartStop::Bits1_5:
        case UartStop::Bits2:
            stop_bits = 2;
            break;
        default:
            stop_bits = 1;
    }

    // Set format: data bits, stop bits, parity
    uart_set_format(uart, data_bits, stop_bits, parity_mode);
}

bool uart_half_duplex(uint32_t uart_num) {
    // RP2040 UART does not have native half-duplex support
    // Return true to indicate error/unsupported
    return true;
}

int uart_read(uint32_t uart_num, uint8_t* buf, uint32_t len, uint32_t timeout_ms) {
    if (uart_num >= 2 || buf == nullptr || len == 0) {
        return 0;
    }

    uint32_t bytes_read = 0;
    uint32_t start_ms   = to_ms_since_boot(get_absolute_time());

    while (bytes_read < len) {
        bool     popped    = false;
        uint32_t irq_state = save_and_disable_interrupts();
        auto     rx        = &rx_info[uart_num];
        if (rx->count > 0) {
            buf[bytes_read++] = rx->queue[rx->tail];
            rx->tail          = (uint16_t)((rx->tail + 1u) & (UART_RX_QUEUE_SIZE - 1u));
            rx->count--;
            popped = true;
        }
        restore_interrupts(irq_state);

        if (popped) {
            continue;
        }

        if (timeout_ms == 0) {
            break;
        }
        if ((to_ms_since_boot(get_absolute_time()) - start_ms) >= timeout_ms) {
            break;
        }
        sleep_us(100);
    }

    return bytes_read;
}

int uart_write(uint32_t uart_num, const uint8_t* buf, size_t len) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr || buf == nullptr) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uart_putc(uart, buf[i]);
    }

    return len;
}

void uart_xon(uint32_t uart_num) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return;
    }
    
    // Send XON character (0x11)
    uart_putc(uart, 0x11);
}

void uart_xoff(uint32_t uart_num) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return;
    }
    
    // Send XOFF character (0x13)
    uart_putc(uart, 0x13);
}

void uart_sw_flow_control(uint32_t uart_num, bool on, uint32_t xon_threshold, uint32_t xoff_threshold) {
    if (uart_num < 2) {
        flow_control[uart_num].enabled         = on;
        flow_control[uart_num].xon_threshold   = xon_threshold;
        flow_control[uart_num].xoff_threshold  = xoff_threshold;
    }
}

bool uart_pins(uint32_t uart_num, pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin, pinnum_t cts_pin) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return true;
    }

    // Set TX pin
    if (tx_pin != static_cast<pinnum_t>(-1)) {
        gpio_set_function(tx_pin, GPIO_FUNC_UART);
    }

    // Set RX pin
    if (rx_pin != static_cast<pinnum_t>(-1)) {
        gpio_set_function(rx_pin, GPIO_FUNC_UART);
    }

    // For RTS/CTS, we would need to set them up separately
    // RP2040 supports hardware flow control on certain pins
    // This is a simplified implementation that ignores RTS/CTS for now
    // In a full implementation, you would check if the pins support UART flow control
    // and configure them appropriately

    return false;  // Success
}

int uart_buflen(uint32_t uart_num) {
    if (uart_num >= 2) {
        return 0;
    }
    uint32_t irq_state = save_and_disable_interrupts();
    auto     rx        = &rx_info[uart_num];
    int      used      = rx->count;
    restore_interrupts(irq_state);
    return used;
}

int uart_bufavail(uint32_t uart_num) {
    int used = uart_buflen(uart_num);
    return UART_RX_QUEUE_SIZE - used;
}

void uart_discard_input(uint32_t uart_num) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr || uart_num >= 2) {
        return;
    }

    uint32_t irq_state = save_and_disable_interrupts();
    auto     rx        = &rx_info[uart_num];
    rx->head           = 0;
    rx->tail           = 0;
    rx->count          = 0;
    restore_interrupts(irq_state);

    // Flush any bytes still in the hardware FIFO
    while (uart_is_readable(uart)) {
        uart_getc(uart);
    }
}

bool uart_wait_output(uint32_t uart_num, uint32_t timeout_ms) {
    uart_inst_t* uart = get_uart_instance(uart_num);
    if (uart == nullptr) {
        return false;
    }

    uint32_t start_ms = to_ms_since_boot(get_absolute_time());

    // Wait for UART TX FIFO to be empty
    while (!uart_is_writable(uart)) {
        // Check timeout
        if (timeout_ms > 0) {
            uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - start_ms;
            if (elapsed >= timeout_ms) {
                return true;  // Timeout occurred
            }
        }
        sleep_us(10);
    }

    return false;  // Success
}


