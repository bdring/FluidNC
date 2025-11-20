#include <stdio.h>
#include <Driver/fluidnc_uart.h>

#include <driver/uart.h>
#include "fnc_idf_uart.h"
#include <esp_ipc.h>
#include "hal/uart_hal.h"
#include "Protocol.h"

const int PINNUM_MAX                        = 64;
InputPin* objects[UART_NUM_MAX][PINNUM_MAX] = { nullptr };
uint8_t   last[UART_NUM_MAX]                = { 0 };

void uart_data_callback(uart_port_t uart_num, uint8_t* buf, int* len) {
    size_t in_len = size_t(*len);
    size_t in, out;
    for (in = 0, out = 0; in < in_len; in++, out++) {
        uint8_t c = buf[in];
        if (out != in) {
            buf[out] = c;
        }
        if (last[uart_num]) {
            --out;
            pinnum_t pinnum = c & (PINNUM_MAX - 1);
            protocol_send_event_from_ISR(last[uart_num] == 0xc4 ? &pinInactiveEvent : &pinActiveEvent, (void*)objects[uart_num][pinnum]);
            last[uart_num] = 0;
        } else {
            if (c == 0xc4 || c == 0xc5) {
                --out;
                last[uart_num] = c;
            }
        }
    }
    *len = int(out);
}
void uart_register_input_pin(uint32_t uart_num, pinnum_t pinnum, InputPin* object) {
    objects[uart_num][pinnum] = object;
    last[uart_num]            = 0;
}

static void uart_driver_n_install(void* arg) {
    uart_port_t port = *((uart_port_t*)arg);
    if (port) {
        fnc_uart_driver_install(port, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    } else {
        uart_driver_install(port, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    }
}

void uart_init(uint32_t uart_num) {
    uart_port_t port = (uart_port_t)uart_num;

    // For UART0, install directly on the current core (usually the main core)
    // For other UARTs, use core 0 to avoid StepTimer conflicts
    if (uart_num == 0) {
        // Install UART0 on the current core so ISR can signal this task properly
        uart_driver_n_install(&port);
    } else {
        // We init other UARTs on core 0 so the interrupt handler runs there,
        // thus avoiding conflicts.
        esp_ipc_call_blocking(0, uart_driver_n_install, &uart_num);
        fnc_uart_set_data_callback(port, uart_data_callback);
    }
}

std::map<UartData, uart_word_length_t> datalen = {
    { UartData::Bits5, UART_DATA_5_BITS },
    { UartData::Bits6, UART_DATA_6_BITS },
    { UartData::Bits7, UART_DATA_7_BITS },
    { UartData::Bits8, UART_DATA_8_BITS },
};

std::map<UartStop, uart_stop_bits_t> stopbits = {
    { UartStop::Bits1, UART_STOP_BITS_1 },
    { UartStop::Bits1, UART_STOP_BITS_1_5 },
    { UartStop::Bits2, UART_STOP_BITS_2 },
};

std::map<UartParity, uart_parity_t> parity_mode = {
    { UartParity::None, UART_PARITY_DISABLE },
    { UartParity::Even, UART_PARITY_EVEN },
    { UartParity::Odd, UART_PARITY_ODD },
};

void uart_mode(uint32_t uart_num, uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits) {
    uart_config_t conf;
    conf.source_clk          = UART_SCLK_APB;
    conf.baud_rate           = baud;
    conf.data_bits           = datalen[dataBits];
    conf.parity              = parity_mode[parity];
    conf.stop_bits           = stopbits[stopBits];
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;

    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        fnc_uart_param_config(port, &conf);
    } else {
        uart_param_config(port, &conf);
    }
}

bool uart_half_duplex(uint32_t uart_num) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        return fnc_uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK;
    } else {
        return uart_set_mode(port, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK;
    }
}

int uart_read(uint32_t uart_num, uint8_t* buf, uint32_t len, uint32_t timeout_ms) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        return fnc_uart_read_bytes(port, buf, len, timeout_ms);
    } else {
        return uart_read_bytes(port, buf, len, timeout_ms);
    }
}
int uart_write(uint32_t uart_num, const uint8_t* buf, size_t len) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        return fnc_uart_write_bytes(port, buf, len);
    } else {
        return uart_write_bytes(port, buf, len);
    }
}
void uart_xon(uint32_t uart_num) {
    uart_port_t port = (uart_port_t)uart_num;
    uart_ll_force_xon(port);
}
void uart_xoff(uint32_t uart_num) {
    uart_port_t port = (uart_port_t)uart_num;
    uart_ll_force_xoff(port);
}
void uart_sw_flow_control(uint32_t uart_num, bool on, uint32_t xon_threshold, uint32_t xoff_threshold) {
    if (xon_threshold == 0) {
        xon_threshold = 126;
    }
    if (xoff_threshold == 0) {
        xoff_threshold = 127;
    }
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        fnc_uart_set_sw_flow_ctrl(port, on, xon_threshold, xoff_threshold);
    } else {
        uart_set_sw_flow_ctrl(port, on, xon_threshold, xoff_threshold);
    }
}
bool uart_pins(uint32_t uart_num, pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin, pinnum_t cts_pin) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        return fnc_uart_set_pin(uart_port_t(uart_num), tx_pin, rx_pin, rts_pin, cts_pin) != ESP_OK;
    } else {
        return uart_set_pin(uart_port_t(uart_num), tx_pin, rx_pin, rts_pin, cts_pin) != ESP_OK;
    }
}
int uart_bufavail(uint32_t uart_num) {
#ifdef UART_HW_FIFO_LEN  // Apparently not all uarts have the same FIFO buffer.
    return UART_HW_FIFO_LEN(uart_num) - uart_buflen(uart_num);
#else
    return UART_FIFO_LEN - uart_buflen(uart_num);
#endif
}
int uart_buflen(uint32_t uart_num) {
    size_t      size;
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        fnc_uart_get_buffered_data_len(port, &size);
    } else {
        uart_get_buffered_data_len(port, &size);
    }
    return size;
}
void uart_discard_input(uint32_t uart_num) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        fnc_uart_flush_input(port);
    } else {
        uart_flush_input(port);
    }
}
bool uart_wait_output(uint32_t uart_num, uint32_t timeout_ms) {
    uart_port_t port = (uart_port_t)uart_num;
    if (port) {
        return fnc_uart_wait_tx_done(port, timeout_ms) != ESP_OK;
    } else {
        return uart_wait_tx_done(port, timeout_ms) != ESP_OK;
    }
}
