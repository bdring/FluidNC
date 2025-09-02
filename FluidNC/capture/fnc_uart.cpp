#include <Driver/fluidnc_uart.h>

#include "UartTypes.h"

#include "Capture.h"

#include <sstream>
#include <algorithm>

class InputPin;
void uart_register_input_pin(int uart_num, uint8_t pinnum, InputPin* object) {}

inline std::string uart_key(int uart_num) {
    std::ostringstream key;
    key << "uart." << uart_num;

    return key.str();
}

void uart_discard_input(int uart_num) {}

void uart_init(int uart_num) {}

int uart_buflen(int uart_num) {
    auto        key = uart_key(uart_num);
    const auto& val = Inputs::instance().get(key);
    return val.size();
}

extern int inchar();

int uart_read(int uart_num, uint8_t* buf, int len, int timeout_ms) {
    auto        key = uart_key(uart_num);
    const auto& val = Inputs::instance().get(key);
    auto        max = std::min(size_t(len), val.size());
    for (size_t i = 0; i < max; ++i) {
        buf[i] = uint8_t(val[i]);
    }
    std::vector<uint32_t> newval(val.begin() + max, val.end());
    Inputs::instance().set(key, newval);
    return int(max);
}

int uart_write(int uart_num, const uint8_t* buf, int len) {
    auto key = uart_key(uart_num);
    auto val = Inputs::instance().get(key);
    for (size_t i = 0; i < len; ++i) {
        val.push_back(uint32_t(uint8_t(buf[i])));
    }
    Inputs::instance().set(key, val);
    return int(len);
}

void uart_mode(int uart_num, unsigned long baud, UartData dataBits, UartParity parity, UartStop stopBits) {}

bool uart_half_duplex(int uart_num) {
    return true;
}

void uart_xon(int uart_num) {}

void uart_xoff(int uart_num) {}

void uart_sw_flow_control(int uart_num, bool on, int xon_threshold, int xoff_threshold) {}

bool uart_pins(int uart_num, int tx_pin, int rx_pin, int rts_pin, int cts_pin) {
    return false;
}

int uart_bufavail(int uart_num) {
    return 128 - uart_buflen(uart_num);
}

bool uart_wait_output(int uart_num, int timeout_ms) {
    return true;
}
