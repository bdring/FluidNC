#include <stdio.h>
#include <Driver/fluidnc_uart.h>

#include <driver/uart.h>
#include <esp_ipc.h>
#include "hal/uart_hal.h"

static void uart_driver_n_install(void* arg) {
    uart_driver_install((uart_port_t)arg, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
}

void uart_init(int uart_num) {
    // We init UARTs on core 0 so the interrupt handler runs there,
    // thus avoiding conflict with the StepTimer interrupt
    esp_ipc_call_blocking(0, uart_driver_n_install, (void*)uart_num);
}

void uart_mode(int uart_num, unsigned long baud, UartData dataBits, UartParity parity, UartStop stopBits) {
    uart_config_t conf;
    conf.source_clk          = UART_SCLK_APB;
    conf.baud_rate           = baud;
    conf.data_bits           = uart_word_length_t(dataBits);
    conf.parity              = uart_parity_t(parity);
    conf.stop_bits           = uart_stop_bits_t(stopBits);
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    uart_param_config(uart_port_t(uart_num), &conf);
}

bool uart_half_duplex(int uart_num) {
    return uart_set_mode(uart_port_t(uart_num), UART_MODE_RS485_HALF_DUPLEX) != ESP_OK;
}

int uart_read(int uart_num, uint8_t* buf, int len, int timeout_ms) {
    return uart_read_bytes(uart_port_t(uart_num), buf, len, timeout_ms);
}
int uart_write(int uart_num, const uint8_t* buf, int len) {
    return uart_write_bytes(uart_port_t(uart_num), buf, len);
}
void uart_xon(int uart_num) {
    uart_ll_force_xon(uart_port_t(uart_num));
}
void uart_xoff(int uart_num) {
    uart_ll_force_xoff(uart_port_t(uart_num));
}
void uart_sw_flow_control(int uart_num, bool on, int xon_threshold, int xoff_threshold) {
    if (xon_threshold <= 0) {
        xon_threshold = 126;
    }
    if (xoff_threshold <= 0) {
        xoff_threshold = 127;
    }
    uart_set_sw_flow_ctrl(uart_port_t(uart_num), on, xon_threshold, xoff_threshold);
}
bool uart_pins(int uart_num, int tx_pin, int rx_pin, int rts_pin, int cts_pin) {
    return uart_set_pin(uart_num, tx_pin, rx_pin, rts_pin, cts_pin) != ESP_OK;
}
int uart_buflen(int uart_num) {
    size_t size;
    uart_get_buffered_data_len(uart_port_t(uart_num), &size);
    return size;
}
void uart_discard_input(int uart_num) {
    uart_flush_input(uart_port_t(uart_num));
}
bool uart_wait_output(int uart_num, int timeout_ms) {
    return uart_wait_tx_done(uart_port_t(uart_num), timeout_ms) != ESP_OK;
}
