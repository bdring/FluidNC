// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

/*
 * UART driver that accesses the ESP32 hardware FIFOs directly.
 */

#include "Uart.h"

#include <driver/uart.h>
#include <esp_ipc.h>

Uart::Uart(int uart_num, bool addCR) : Channel("uart", addCR) {
    // Auto-assign Uart harware engine numbers; the pins will be
    // assigned to the engines separately
    static int currentNumber = 1;
    if (uart_num == -1) {
        Assert(currentNumber <= 3, "Max number of UART's reached.");
        uart_num = currentNumber++;
    } else {
        _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    }
    _uart_num = uart_port_t(uart_num);
}

// Use the specified baud rate
void Uart::begin(unsigned long baudrate) {
    auto txd = _txd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto rxd = _rxd_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);
    auto rts = _rts_pin.undefined() ? -1 : _rts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Output);
    auto cts = _cts_pin.undefined() ? -1 : _cts_pin.getNative(Pin::Capabilities::UART | Pin::Capabilities::Input);

    if (setPins(txd, rxd, rts, cts)) {
        Assert(false, "Uart pin config failed");
        return;
    }

    begin(baudrate, dataBits, stopBits, parity);
}

// Use the configured baud rate
void Uart::begin() {
    begin(static_cast<unsigned long>(baud));
}

static void uart_driver_n_install(void* arg) {
    uart_driver_install((uart_port_t)arg, 256, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
}

void Uart::begin(unsigned long baudrate, UartData dataBits, UartStop stopBits, UartParity parity) {
    //    uart_driver_delete(_uart_num);
    uart_config_t conf;
    conf.baud_rate           = baudrate;
    conf.data_bits           = uart_word_length_t(dataBits);
    conf.parity              = uart_parity_t(parity);
    conf.stop_bits           = uart_stop_bits_t(stopBits);
    conf.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    conf.rx_flow_ctrl_thresh = 0;
    if (uart_param_config(_uart_num, &conf) != ESP_OK) {
        // TODO FIXME - should this throw an error?
        return;
    };

    // We init the UART on core 0 so the interrupt handler runs there,
    // thus avoiding conflict with the StepTimer interrupt
    esp_ipc_call_blocking(0, uart_driver_n_install, (void*)_uart_num);
}

int Uart::available() {
    size_t size = 0;
    uart_get_buffered_data_len(_uart_num, &size);
    return size + (_pushback != -1);
}

int Uart::peek() {
    if (_pushback != -1) {
        return _pushback;
    }
    int ch = read();
    if (ch == -1) {
        return -1;
    }
    _pushback = ch;
    return ch;
}

int Uart::read(TickType_t timeout) {
    if (_pushback != -1) {
        int ret   = _pushback;
        _pushback = -1;
        return ret;
    }
    uint8_t c;
    int     res = uart_read_bytes(_uart_num, &c, 1, timeout);
    return res == 1 ? c : -1;
}

int Uart::read() {
    return read(0);
}

int Uart::rx_buffer_available() {
    return UART_FIFO_LEN - available();
}

bool Uart::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool Uart::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Channel* Uart::pollLine(char* line) {
    // UART0 is the only Uart instance that can be a channel input device
    // Other UART users like RS485 use it as a dumb character device
    if (_lineedit == nullptr) {
        return nullptr;
    }
    return Channel::pollLine(line);
}

size_t Uart::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    size_t remlen = length;
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
    }

    int res = uart_read_bytes(_uart_num, (uint8_t*)buffer, remlen, timeout);
    // If res < 0, no bytes were read
    remlen -= (res < 0) ? 0 : res;
    return length - remlen;
}
size_t Uart::write(uint8_t c) {
    // Use Uart::write(buf, len) instead of uart_write_bytes() for _addCR
    return write(&c, 1);
}

size_t Uart::write(const uint8_t* buffer, size_t length) {
    // Replace \n with \r\n
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            char      modbuf[bufsize];
            // bufsize-1 in case the last character is \n
            size_t k = 0;
            while (rem && k < (bufsize - 1)) {
                char c = buffer[j++];
                if (c == '\n' && lastchar != '\r') {
                    modbuf[k++] = '\r';
                }
                lastchar    = c;
                modbuf[k++] = c;
                --rem;
            }

            uart_write_bytes(_uart_num, (const char*)modbuf, k);
        }
    } else {
        uart_write_bytes(_uart_num, (const char*)buffer, length);
    }
    return length;
}

// size_t Uart::write(const char* text) {
//    return uart_write_bytes(_uart_num, text, strlen(text));
// }

bool Uart::setHalfDuplex() {
    return uart_set_mode(_uart_num, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK;
}
bool Uart::setPins(int tx_pin, int rx_pin, int rts_pin, int cts_pin) {
    return uart_set_pin(_uart_num, tx_pin, rx_pin, rts_pin, cts_pin) != ESP_OK;
}
bool Uart::flushTxTimed(TickType_t ticks) {
    return uart_wait_tx_done(_uart_num, ticks) != ESP_OK;
}

Uart Uart0(0, true);  // Primary serial channel with LF to CRLF conversion

void uartInit() {
    Uart0.begin(BAUD_RATE, UartData::Bits8, UartStop::Bits1, UartParity::None);
}

void Uart::config_message(const char* prefix, const char* usage) {
    log_info(prefix << usage << "Uart Tx:" << _txd_pin.name() << " Rx:" << _rxd_pin.name() << " RTS:" << _rts_pin.name() << " Baud:" << baud);
}

void Uart::flushRx() {
    _pushback = -1;
    uart_flush_input(_uart_num);
    Channel::flushRx();
}
