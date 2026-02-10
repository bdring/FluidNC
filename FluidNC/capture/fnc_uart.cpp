#include "Platform.h"
#include "Driver/fluidnc_uart.h"

#include "UartTypes.h"

#include "Capture.h"

#include <sstream>
#include <algorithm>
#include "HuanyangSimulator.h"
#include "NutsBolts.h"  // get_ms()

class InputPin;
void uart_register_input_pin(uint32_t uart_num, pinnum_t pinnum, InputPin* object) {}

VFDSimulator* vfd_simulator[MAX_N_UARTS];

inline std::string uart_key(uint32_t uart_num) {
    std::ostringstream key;
    key << "uart." << uart_num;

    return key.str();
}

void uart_discard_input(uint32_t uart_num) {}

void uart_init(uint32_t uart_num) {}

int uart_buflen(uint32_t uart_num) {
    auto        key = uart_key(uart_num);
    const auto& val = Inputs::instance().get(key);
    return val.size();
}

extern int inchar();

std::vector<uint8_t> vfd_output;
uint32_t             vfd_ms;

int uart_read(uint32_t uart_num, uint8_t* buf, uint32_t len, uint32_t timeout_ms) {
    if (vfd_simulator[uart_num]) {
        if (vfd_output.size()) {
            auto copylen = std::min((size_t)len, vfd_output.size());
            std::copy(vfd_output.begin(), vfd_output.begin() + copylen, buf);
            vfd_output.erase(vfd_output.begin(), vfd_output.begin() + copylen);
            return copylen;
        }
        return 0;
    }
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

int uart_write(uint32_t uart_num, const uint8_t* buf, size_t len) {
    if (vfd_simulator[uart_num]) {
        int32_t this_ms = get_ms();
        vfd_simulator[uart_num]->update((int32_t)this_ms - (int32_t)vfd_ms);
        vfd_ms     = this_ms;
        vfd_output = vfd_simulator[uart_num]->processModbusMessage(std::vector<uint8_t> { buf, buf + len });

        return 0;
    }

    auto key = uart_key(uart_num);
    auto val = Inputs::instance().get(key);
    for (size_t i = 0; i < len; ++i) {
        val.push_back(uint32_t(uint8_t(buf[i])));
    }
    Inputs::instance().set(key, val);
    return int(len);
}

void uart_mode(uint32_t uart_num, uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits) {}

bool uart_half_duplex(uint32_t uart_num) {
    if (vfd_simulator[uart_num]) {
        delete vfd_simulator[uart_num];
        vfd_simulator[uart_num] = nullptr;
    }
    vfd_simulator[uart_num] = new VFDSimulator();
    vfd_ms                  = get_ms();

    return false;
}

void uart_xon(uint32_t uart_num) {}

void uart_xoff(uint32_t uart_num) {}

void uart_sw_flow_control(uint32_t uart_num, bool on, uint32_t xon_threshold, uint32_t xoff_threshold) {}

bool uart_pins(uint32_t uart_num, pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin, pinnum_t cts_pin) {
    return false;
}

int uart_bufavail(uint32_t uart_num) {
    return 128 - uart_buflen(uart_num);
}

bool uart_wait_output(uint32_t uart_num, uint32_t timeout_ms) {
    return true;
}
