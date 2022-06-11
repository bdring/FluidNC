#include "uart.h"

#include "../Capture.h"
#include "../esp_err.h"

#include <sstream>
#include <algorithm>

inline std::string uart_key(uart_port_t uart_num) {
    std::ostringstream key;
    key << "uart." << uart_num;

    return key.str();
}

esp_err_t uart_flush(uart_port_t uart_num) {
    return ESP_OK;
}
esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t* uart_config) {
    return ESP_OK;
}
esp_err_t uart_driver_install(
    uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size, int queue_size, QueueHandle_t* uart_queue, int intr_alloc_flags) {
    return ESP_OK;
}

esp_err_t uart_get_buffered_data_len(uart_port_t uart_num, size_t* size) {
    auto        key = uart_key(uart_num);
    const auto& val = Inputs::instance().get(key);
    *size           = val.size();
    return ESP_OK;
}

int uart_read_bytes(uart_port_t uart_num, uint8_t* buf, uint32_t length, TickType_t ticks_to_wait) {
    auto        key = uart_key(uart_num);
    const auto& val = Inputs::instance().get(key);
    auto        max = std::min(size_t(length), val.size());
    for (size_t i = 0; i < max; ++i) {
        buf[i] = uint8_t(val[i]);
    }
    std::vector<uint32_t> newval(val.begin() + max, val.end());
    Inputs::instance().set(key, newval);
    return int(max);
}

int uart_write_bytes(uart_port_t uart_num, const char* src, size_t size) {
    auto key = uart_key(uart_num);
    auto val = Inputs::instance().get(key);
    for (size_t i = 0; i < size; ++i) {
        val.push_back(uint32_t(uint8_t(src[i])));
    }
    Inputs::instance().set(key, val);
    return int(size);
}
esp_err_t uart_set_mode(uart_port_t uart_num, uart_mode_t mode) {
    return ESP_OK;
}
esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num, int cts_io_num) {
    return ESP_OK;
}
esp_err_t uart_wait_tx_done(uart_port_t uart_num, TickType_t ticks_to_wait) {
    return ESP_OK;
}
