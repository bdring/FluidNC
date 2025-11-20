#if 0
#    include "RGBLed.h"

#    include "System.h"

namespace Listeners {
    RGBLed::RGBLed() : SysListener("rgbled") {}

    void RGBLed::init() {
        log_info("Initializing RGB Led on gpio " << pin_ << ", index " << index_);

        if (this->pin_.defined()) {
            auto thepin = this->pin_.getNative(Pin::Capabilities::Native | Pin::Capabilities::Output);

            pixels_ = new Adafruit_NeoPixel(index_ + 1, thepin, NEO_GRB + NEO_KHZ800);
            pixels_->begin();
            pixels_->clear();
            pixels_->setPixelColor(0, pixels_->Color(32, 0, 0));  // Booting is dark red. You have no choice in the matter.
            pixels_->show();

            sys.register_change_handler(handleChange, this);
        }
    }

    void RGBLed::handleChangeDetail(SystemDirty changes, const system_t& state) {
        if ((int(changes) & int(SystemDirty::State)) != 0) {
            uint32_t index = index_;
            int32_t value = -1;

            switch (state.state()) {
                case State::Idle:
                    value = this->idle;
                    break;
                case State::Alarm:
                    value = this->alarm;
                    break;
                case State::CheckMode:
                    value = this->checkMode;
                    break;
                case State::Homing:
                    value = this->homing;
                    break;
                case State::Cycle:
                    value = this->cycle;
                    break;
                case State::Hold:
                    value = this->hold;
                    break;
                case State::Jog:
                    value = this->jog;
                    break;
                case State::SafetyDoor:
                    value = this->safetyDoor;
                    break;
                case State::Sleep:
                    value = this->sleep;
                    break;
                case State::ConfigAlarm:
                    value = this->configAlarm;
                    break;
                default:
                    value = -1;
                    break;
            }

            // log_info("Updating RGB led to " << (value < 0 ? "none" : getColor(value)));

            if (value >= 0) {
                pixels_->setPixelColor(index, pixels_->Color((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF));
                pixels_->show();
            }
        }
    }

    // Configuration registration
    namespace {
        SysListenerFactory::InstanceBuilder<RGBLed> registration("rgbled");
    }
}
#endif
