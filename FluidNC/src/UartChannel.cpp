// Copyright (c) 2023 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "UartChannel.h"
#include "Driver/Console.h"
#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels
#include "Report.h"                 // report_realtime_status

UartChannel::UartChannel(objnum_t num, bool addCR) : Channel("uart_channel", num, addCR) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
    _active   = false;
}

void UartChannel::init() {
    auto uart = config->_uarts[_uart_num];
    if (!uart) {
        log_error(name() << ": missing uart" << _uart_num);
    } else if (!uart->configured()) {
        log_error(name() << ": uart" << _uart_num << " failed configuration");
    } else {
        init(uart);
    }
    if (!uart || uart->_rxd_pin.undefined()) {
        _active = true;  // there will be no rx activity to set this true
    }
    setReportInterval(_report_interval_ms);
}
void UartChannel::init(Uart* uart) {
    if (!uart || !uart->configured()) {
        log_error(name() << ": cannot initialize with unconfigured UART");
        return;
    }
    _uart             = uart;
    _report_when_idle = true;
    allChannels.registration(this);
    if (_report_interval_ms) {
        log_info(name() << " created at report interval: " << _report_interval_ms);
    } else {
        log_info(name() << " created");
    }
    sendGreeting();
    if (_uart_num) {
        getExpanderId();
    }
}

void UartChannel::sendGreeting() {
    // Tell the channel listener that FluidNC has restarted.
    // The initial newline clears out any garbage characters that might have
    // resulted from the UART initialization and turn-on
    print("\n");
    out("RST", "MSG:");
    _last_greeting_ms = millis();
}

static const uint32_t greeting_repeat_ms = 1000;

// A device that powers up with FluidNC is often not listening yet when init()
// sends the greeting, and an idle machine sends nothing after it.  If the
// device waits for data before talking, neither side ever starts.  Repeat the
// greeting, with a status report, until a complete line arrives.
void UartChannel::handle() {
    if (_peer_spoke || !_report_interval_ms || _uart->_rxd_pin.undefined()) {
        return;
    }
    if ((millis() - _last_greeting_ms) < greeting_repeat_ms) {
        return;
    }
    sendGreeting();
    report_realtime_status(*this);
}

// An expander answers the ID query immediately; bound the wait so that a
// device that keeps transmitting cannot stall startup.
static const uint32_t expander_id_timeout_ms = 100;

static const char*  expander_prefix = "(EXP,";
static const size_t prefix_len      = 5;

void UartChannel::getExpanderId() {
    out("ID", "EXP:");

    // A read can return a partial reply - at 9600 baud the expander's answer
    // takes longer than one read timeout - so accumulate until the reply is
    // complete, or until it is clear that this is not an expander answering.
    char     buf[128];
    size_t   len            = 0;
    bool     maybe_expander = true;
    uint32_t start          = millis();
    while ((millis() - start) < expander_id_timeout_ms) {
        size_t got = _uart->timedReadBytes(buf + len, sizeof(buf) - 1 - len, 10);
        if (!got) {
            continue;
        }
        len += got;
        buf[len] = '\0';

        if (maybe_expander) {
            size_t matchable = len < prefix_len ? len : prefix_len;
            maybe_expander   = strncmp(buf, expander_prefix, matchable) == 0;
        }
        if (maybe_expander) {
            auto pos = len >= prefix_len ? strrchr(buf, ')') : nullptr;
            if (pos) {
                *pos = '\0';
                print("ok\n");
                log_info("IO Expander " << &buf[prefix_len]);
                _peer_spoke = true;
                return;
            }
            // Still a possible prefix of a reply.  Wait for the rest, unless
            // there is no room left, in which case it is not one.
            if (len < sizeof(buf) - 1) {
                continue;
            }
            maybe_expander = false;
        }

        // Not an expander reply, so it belongs to whatever else is on the port,
        // typically a pendant's opening message.  These reads bypass pollLine(),
        // so pass the bytes to the normal input path instead of dropping them.
        queueInput(buf, len);
        len = 0;
        // Anything that follows could still be the expander's reply, so start
        // classifying again from the next read.
        maybe_expander = true;
    }
    // Timed out part way through something that still looked like a reply.  It
    // was not one, so it is input like any other.
    queueInput(buf, len);
}

void UartChannel::queueInput(const char* buf, size_t len) {
    if (!len) {
        return;
    }
    for (size_t i = 0; i < len; i++) {
        queue_push(static_cast<uint8_t>(buf[i]));
    }
    _active = true;
}

size_t UartChannel::write(uint8_t c) {
    return _uart->write(c);
}

size_t UartChannel::write(const uint8_t* buffer, size_t length) {
    // Replace \n with \r\n
    if (_addCR) {
        size_t rem      = length;
        char   lastchar = '\0';
        size_t j        = 0;
        while (rem) {
            const int bufsize = 80;
            uint8_t   modbuf[bufsize];
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
            _uart->write(modbuf, k);
        }
        return length;
    } else {
        return _uart->write(buffer, length);
    }
}

int UartChannel::available() {
    return _uart->available();
}

int UartChannel::peek() {
    return _uart->peek();
}

int UartChannel::rx_buffer_available() {
    return _uart->rx_buffer_available();
}

bool UartChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool UartChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _peer_spoke     = true;
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

int UartChannel::read() {
    auto c = _uart->read();
    if (c == 0x11) {
        // 0x11 is XON.  If we receive that, it is a request to use software flow control
        // 0 0 means use default values from uart.cpp
        _uart->setSwFlowControl(true, 0, 0);
        return -1;
    }
    return c;
}

void UartChannel::flushRx() {
    _uart->flushRx();
    Channel::flushRx();
}

size_t UartChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;

    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    uint8_t queued = 0;
    while (remlen && try_pop_queued_byte(queued)) {
        *buffer++ = queued;
        --remlen;
    }

    auto thislen = _uart->timedReadBytes(buffer, remlen, timeout);
    remlen -= thislen;

    return length - remlen;
}

void UartChannel::out(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

void UartChannel::out_acked(const std::string& s, const char* tag) {
    log_stream(*this, "[" << tag << s);
}

void UartChannel::beginJSON(const char* json_tag) {
    //    out_acked(json_tag, "JSONBEGIN:");
}
void UartChannel::endJSON(const char* json_tag) {
    //    out_acked(json_tag, "JSONEND:");
}

void UartChannel::registerEvent(pinnum_t pinnum, InputPin* obj) {
    Channel::registerEvent(pinnum, obj);  // Establish the handler function first
    _uart->registerInputPin(pinnum, obj);
}

bool UartChannel::setAttr(pinnum_t index, bool* value, const std::string& attrString) {
    out(attrString, "EXP:");
    _ackwait = 1;
    for (size_t i = 0; i < 75; i++) {
        pollLine(nullptr);
        if (_ackwait < 1) {
            return _ackwait == 0;
        }
        delay_ms(1);
    }
    _ackwait = 0;
    log_error("IO Expander is unresponsive");
    return false;
}
