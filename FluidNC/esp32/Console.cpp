#if ARDUINO_USB_CDC_ON_BOOT
#    include "USBCDCChannel.h"
USBCDCChannel CDCUart(true);
Channel&      Console = CDCUart;
#else
#    include "UartChannel.h"
UartChannel Uart0(true);
Channel&    Console = Uart0;
#endif
