#include "Midtbot.h"
/*


*/

namespace Kinematics {
    void Midtbot::group(Configuration::HandlerBase& handler) {}

    void Midtbot::init() {
        _x_scaler = 2.0;
        log_info("Kinematic system: " << name());
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Midtbot> registration("midtbot");
    }
}
