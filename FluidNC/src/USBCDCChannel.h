#pragma once

#ifndef ARDUINO_USB_CDC_ON_BOOT
#    define ARDUINO_USB_CDC_ON_BOOT 0
#endif
#if ARDUINO_USB_CDC_ON_BOOT  //Serial used for USB CDC

#    include <USB.h>
#    include <USBCDC.h>
#    include <HWCDC.h>

#    include "Channel.h"
#    include "lineedit.h"

class USBCDCChannel : public Channel {
private:
    Lineedit* _lineedit;
    HWCDC*    _uart;

public:
    USBCDCChannel(bool addCR = false);

    void init();

    // Print methods (Stream inherits from Print)
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buf, size_t len) override;

    // Stream methods (Channel inherits from Stream)
    int peek(void) override;
    int available(void) override;
    int read() override;

    // Channel methods
    int      rx_buffer_available() override;
    void     flushRx() override;
    size_t   timedReadBytes(char* buffer, size_t length, TickType_t timeout);
    size_t   timedReadBytes(uint8_t* buffer, size_t length, TickType_t timeout) { return timedReadBytes((char*)buffer, length, timeout); };
    bool     realtimeOkay(char c) override;
    bool     lineComplete(char* line, char c) override;
    Channel* pollLine(char* line) override;
};

#    ifndef ARDUINO_USB_CDC_ON_BOOT
#        define ARDUINO_USB_CDC_ON_BOOT 0
#    endif

#    if ARDUINO_USB_CDC_ON_BOOT  //Serial used for USB CDC
extern USBCDCChannel Uart0;

extern void uartInit();
#    endif
#endif
