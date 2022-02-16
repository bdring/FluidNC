#include "../Uart.h"
#include "../Error.h"  // Error

namespace Displays {
    class Nextion : public Uart {

        public:
            void ack(Error status) override {};  // don't send acks to the display
    };
}