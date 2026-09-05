#include <sdkconfig.h>
#include <esp_idf_version.h>

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && defined(IDFBUILD)
#    include "USBCDCChannel_IDF.h"
#else
#    include "USBCDCChannel.h"
#endif

USBCDCChannel CDCChannel(true);

#include "UartChannel.h"

#include "Settings.h"
#include "NutsBolts.h"  // delay_ms

// This derived class overrides init() to setup the primary UART
class UartConsole : public UartChannel {
public:
    UartConsole() : UartChannel(0, true) {}
    void init() override {
        auto uart0 = new Uart(uint32_t(0));
        uart0->begin(BAUD_RATE, UartData::Bits8, UartStop::Bits1, UartParity::None);
        UartChannel::init(uart0);

        // Init CDC from NVS web setting (before config load so boot
        // diagnostics are visible on boards without a USB-serial chip).
        // If USB host is later configured, UsbHostUart::begin() tears
        // down TinyUSB and takes over the USB port.
        static auto* cdc_enable = new EnumSetting("USB CDC Enable", WEBSET, WG, NULL, "USBCDC/Enable", true, &onoffOptions);
        if (cdc_enable->get()) {
            CDCChannel.init();
            // Give a USB CDC terminal emulator time to enumerate and connect
            // before any startup/boot messages are sent, so boards with only
            // a USB CDC serial port (no separate USB-serial chip) don't lose
            // their early log output to a terminal that hasn't opened yet.
            delay_ms(1000);
        }
    }
};
UartConsole Uart0;
Channel&    Console = Uart0;
