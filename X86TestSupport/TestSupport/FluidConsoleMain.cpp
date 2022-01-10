#include "src/Serial.h"
#include "src/Settings.h"
#include "src/InputFile.h"
#include "src/Uart.h"
#include "src/Machine/MachineConfig.h"
#include "ComPortX86.h"

extern void setup();
extern void loop();

int main(int argc, char* argv[]) {
    setup();

    // Remove Uart and Web chanalls
    allChannels.deregistration(&Uart0);
    allChannels.deregistration(&WebUI::inputBuffer);

    std::unique_ptr<Channel> pin;
    std::unique_ptr<Channel> pout;

    if (argc == 1) {  //console input
        pin = std::make_unique<ComPortX86>(nullptr);
    } else if (strncasecmp(argv[1], "COM", 3) == 0) {  // com port
        pin = std::make_unique<ComPortX86>(argv[1]);
    } else {
        pout      = std::make_unique<ComPortX86>(nullptr);  // run file from command line, ouput to console
        infile    = new InputFile("/localfs", argv[1], WebUI::AuthenticationLevel::LEVEL_GUEST, *pout);
        readyNext = true;
    }

    if (pin)
        allChannels.registration(pin.get());
    if (pout)
        allChannels.registration(pout.get());

    if (config)
        config->_verboseErrors = true;

    // Unlock GRBL for easy debugging
    do_command_or_setting("X", nullptr, WebUI::AuthenticationLevel::LEVEL_ADMIN, pout != nullptr ? *pout : *pin);

    loop();

    return 0;
}
