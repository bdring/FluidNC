#include "ProbeDriver.h"

namespace Probes {
    void IRAM_ATTR ProbeDriver::tripISR(int32_t tickDelta) { (*callback_)(tickDelta); }

    String ProbeDriver::probeName() const { return probeName_; }

    void ProbeDriver::init(TripProbe* callback) { callback_ = callback; }
    void IRAM_ATTR ProbeDriver::trip(int32_t tickDelta) { (*callback_)(tickDelta); }

    ProbeDriver::~ProbeDriver() {}
}
