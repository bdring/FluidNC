#include <sdkconfig.h>
#include <esp_idf_version.h>

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && ESP_IDF_VERSION_MAJOR >= 5
#    include "USBCDCChannel_IDF.h"
#else
#    include "USBCDCChannel.h"
#endif

USBCDCChannel CDCChannel(true);

#include "UartChannel.h"

#include "Settings.h"

// This derived class overrides init() to setup the primary UART
class UartConsole : public UartChannel {
public:
    UartConsole() : UartChannel(0, true) {}
    void init() override {
        auto uart0 = new Uart(0);
        uart0->begin(BAUD_RATE, UartData::Bits8, UartStop::Bits1, UartParity::None);
        UartChannel::init(uart0);
        // USB CDC/Host initialization is handled post-config in Main.cpp
        // (YAML config must be loaded first to determine USB mode)
    }
};
UartConsole Uart0;
Channel&    Console = Uart0;
