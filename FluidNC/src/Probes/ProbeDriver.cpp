#include "ProbeDriver.h"

namespace Probes {
    void IRAM_ATTR ProbeDriver::tripISR(int32_t tickDelta) { (*callback_)(callbackUserData_, tickDelta); }

    String ProbeDriver::probeName() const { return probeName_; }

    void ProbeDriver::init(TripProbe callback, Probe* userData) {
        callback_         = callback;
        callbackUserData_ = userData;
    }
    void IRAM_ATTR ProbeDriver::trip(int32_t tickDelta) { (*callback_)(callbackUserData_, tickDelta); }

    ProbeDriver::~ProbeDriver() {}
}
