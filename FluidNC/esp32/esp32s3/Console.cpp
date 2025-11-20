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

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && ESP_IDF_VERSION_MAJOR >= 5
        auto cdc_enable = new EnumSetting("USB CDC Enable", WEBSET, WG, NULL, "USBCDC/Enable", true, &onoffOptions);
        if (cdc_enable->get()) {
            CDCChannel.init();
        }
#endif
    }
};
UartConsole Uart0;
Channel&    Console = Uart0;
