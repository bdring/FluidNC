// Copyright (c) 2023 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "USBCDCChannel.h"

#include "Machine/MachineConfig.h"  // config
#include "Serial.h"                 // allChannels
#include "esp32-hal-tinyusb.h"      // usb_persist_restart

// There are two compiler flags that control how the Arduino framework predefines USB CDC serial class instances.
// If ARDUINO_USB_MODE is 1, the TinyUSB variant is preferred, and
//   If ARDUINO_USB_CDC_ON_BOOT it true
//       Serial is a USBCDC (tinyusb)
//       Serial0 is a HardwareSerial (UART)
// Else (ARDUINO_USB_MODE is 0 or undefined)), the hardware CDC is preferred, and
//   If ARDUINO_USB_CDC_ON_BOOT it true
//       Serial is a HWCDC (hardware)
//       Serial0 is a HardwareSerial (UART)
//   Else
//       Serial is a HardwareSerial (UART)
//       USBSerial is a HWCDC (hardware)
USBCDC TUSBCDCSerial;

USBCDCChannel::USBCDCChannel(bool addCR) : Channel("usbcdc", addCR), _cdc(TUSBCDCSerial) {
    _lineedit = new Lineedit(this, _line, Channel::maxLine - 1);
}

static uint32_t state = 0;

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == ARDUINO_USB_EVENTS) {
        arduino_usb_event_data_t* data = (arduino_usb_event_data_t*)event_data;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT:
                ::printf("USB PLUGGED\n");
                break;
            case ARDUINO_USB_STOPPED_EVENT:
                ::printf("USB UNPLUGGED\n");
                break;
            case ARDUINO_USB_SUSPEND_EVENT:
                ::printf("USB SUSPENDED: remote_wakeup_en: %u\n\n", data->suspend.remote_wakeup_en);
                break;
            case ARDUINO_USB_RESUME_EVENT:
                ::printf("USB RESUMED\n");
                break;

            default:
                break;
        }
    } else if (event_base == ARDUINO_USB_CDC_EVENTS) {
        arduino_usb_cdc_event_data_t* data = (arduino_usb_cdc_event_data_t*)event_data;
        switch (event_id) {
            case ARDUINO_USB_CDC_CONNECTED_EVENT:
                break;
            case ARDUINO_USB_CDC_DISCONNECTED_EVENT:
                break;
            case ARDUINO_USB_CDC_LINE_STATE_EVENT: {
                // Keep track of the sequence of line states and emulate the
                // traditional ESP32 RTS/DTR reset behavior.
                // 0=!r!d  1-!rd  2=r!d  3=rd
                state <<= 4;
                state |= ((!!data->line_state.rts) << 1) + (!!data->line_state.dtr);
                state &= 0xfff;

#if DEBUG_ME
                ::putchar(((state >> 8) & 0xf) + '0');
                ::putchar(((state >> 4) & 0xf) + '0');
                ::putchar((state & 0xf) + '0');
                ::putchar('\n');
#endif

                // A sequence of transitions from R1D1 to R0D0 to R1D0
                if (state == 0x302) {
                    usb_persist_restart(RESTART_PERSIST);
                } else if ((state & 0xff) == 0x21) {
                    // A transition from R1D0 to R0D1 reboots to download mode
                    usb_persist_restart(RESTART_BOOTLOADER);
                }
            } break;
            case ARDUINO_USB_CDC_LINE_CODING_EVENT:
#if DEBUG_ME
                ::printf("CDC LINE CODING: bit_rate: %u, data_bits: %u, stop_bits: %u, parity: %u\n\n",
                         data->line_coding.bit_rate,
                         data->line_coding.data_bits,
                         data->line_coding.stop_bits,
                         data->line_coding.parity);
#endif
                break;
            case ARDUINO_USB_CDC_RX_EVENT:
#if DEBUG_ME
                ::printf("CDC RX [%u]:\n", data->rx.len);
                {
                    uint8_t buf[data->rx.len];
                    size_t  len = USBSerial.read(buf, data->rx.len);
                    ::printf("%.*s", buf, len);
                }
                ::printf("\n");
#endif
                break;
            case ARDUINO_USB_CDC_RX_OVERFLOW_EVENT:
                ::printf("CDC RX Overflow of %d bytes\n", data->rx_overflow.dropped_bytes);
                break;

            default:
                break;
        }
    }
}

void USBCDCChannel::init() {
    _cdc.setRxBufferSize(1040);  // 1K + change
    _cdc.begin(115200);
    USB.begin();

    _cdc.enableReboot(false);
    _cdc.onEvent(usbEventCallback);
    USB.onEvent(usbEventCallback);
    delay_ms(300);  // Time for the host USB to reconnect
    allChannels.registration(this);
}

size_t USBCDCChannel::write(uint8_t c) {
    return _cdc.write(c);
}

size_t USBCDCChannel::write(const uint8_t* buffer, size_t length) {
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
            size_t actual = _cdc.write(modbuf, k);
            if (actual != k) {
                // ::printf("dropped %d\n", k - actual);
            }
        }
        return length;
    }
    size_t actual = _cdc.write(buffer, length);
    if (actual != length) {
        // ::printf("dropped %d\n", length - actual);
    }
    return actual;
}

int USBCDCChannel::available() {
    return _cdc.available();
}

int USBCDCChannel::peek() {
    return _cdc.peek();
}

int USBCDCChannel::rx_buffer_available() {
    return 64 - _cdc.available();
}

bool USBCDCChannel::realtimeOkay(char c) {
    return _lineedit->realtime(c);
}

bool USBCDCChannel::lineComplete(char* line, char c) {
    if (_lineedit->step(c)) {
        _linelen        = _lineedit->finish();
        _line[_linelen] = '\0';
        strcpy(line, _line);
        _linelen = 0;
        return true;
    }
    return false;
}

Error USBCDCChannel::pollLine(char* line) {
    if (_lineedit == nullptr) {
        return Error::NoData;
    }
    return Channel::pollLine(line);
}

int USBCDCChannel::read() {
    return _cdc.read();
}

void USBCDCChannel::flushRx() {
    Channel::flushRx();
}

extern Channel& Console;

size_t USBCDCChannel::timedReadBytes(char* buffer, size_t length, TickType_t timeout) {
    size_t remlen = length;

    // It is likely that _queue will be empty because timedReadBytes() is only
    // used in situations where the UART is not receiving GCode commands
    // and Grbl realtime characters.
    while (remlen && _queue.size()) {
        *buffer++ = _queue.front();
        _queue.pop();
        --remlen;
    }

    // The Arduino framework does not expose a timed read function
    // for USBCDC so we have to do the timeout the hard way
    while (remlen && timeout) {
        int thislen = _cdc.read(buffer, remlen);
        if (thislen < 0) {
            // Error
            return 0;
        }
        buffer += thislen;
        remlen -= thislen;
        if (remlen) {
            delay_ms(1);
            timeout -= 1;
        }
    }

    return length - remlen;
}
