// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PwmPin.h"

#include "../Pins/LedcPin.h"
#include "../Pin.h"
#include "../Assert.h"

#include <esp32-hal-ledc.h>  // ledc*

namespace PinUsers {
    class NativePwm : public PwmDetail {
        static NativePwm** PwmChannelResources() {
            // The ESP32 chip has 16 PWM channels, but there are 4 timers, 2 hardware instances per timer and 2
            // software instances per timer.

            static NativePwm* instances_[16] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
            return instances_;
        }

        Pin      pin_;
        int      pwmChannel_;
        uint32_t frequency_;
        uint32_t maxDuty_;
        uint8_t  resolutionBits_;

        /*
            Calculate the highest precision of a PWM based on the frequency in bits

            80,000,000 / freq = period
            determine the highest precision where (1 << precision) < period
        */
        uint8_t calculatePwmPrecision(uint32_t freq) {
            uint8_t precision = 0;

            // increase the precision (bits) until it exceeds allow by frequency the max or is 16
            // TODO is there a named value for the 80MHz?
            while ((1 << precision) < (uint32_t)(80000000 / freq) && precision <= 16) {
                precision++;
            }

            return precision - 1;
        }

        static int TryGrabChannel(uint32_t frequency) {
            auto instances = PwmChannelResources();

            // 1. Attempt to re-use the same frequency
            for (int i = 0; i < 4; ++i) {
                if (instances[i * 2] != nullptr && instances[i * 2 + 1] == nullptr) {
                    // See if this is a frequency match:
                    if (instances[i * 2]->frequency_ == frequency) {
                        return i * 2 + 1;
                    }
                }
            }

            // 2. No luck. Check if we have free slots in the hardware PWM channels / timers
            for (int i = 0; i < 4; ++i) {
                if (instances[i * 2] == nullptr) {
                    return i * 2;
                }
            }

            // 3. Check if we have free slots in the software PWM channels with the same frequency
            for (int i = 0; i < 4; ++i) {
                if (instances[i * 2]->frequency_ == frequency) {
                    if (instances[i * 2 + 8] == nullptr) {
                        return i * 2 + 8;
                    } else if (instances[i * 2 + 9] == nullptr) {
                        // See if this is a frequency match:
                        return i * 2 + 9;
                    }
                }
            }

            // 4. Re-assign hardware PWM channels to software channels.
            // TODO: Implement

            // 5. Nothing is available.
            return -1;
        }

    public:
        NativePwm(Pin&& pin, uint32_t frequency, uint32_t maxDuty) : pin_(std::move(pin)), frequency_(frequency), maxDuty_(maxDuty) {
            auto native = pin.getNative(Pin::Capabilities::PWM | Pin::Capabilities::Native);

            pwmChannel_ = TryGrabChannel(frequency);
            Assert(pwmChannel_ != -1, "PWM Channel could not be claimed. Are all PWM channels in use?");

            resolutionBits_ = calculatePwmPrecision(frequency);

            ledcInit(pin, pwmChannel_, frequency, resolutionBits_);
            ledcSetDuty(pwmChannel_, 0);

            // log_info("PWM Output:" << _number << " on Pin:" << _pin.name() << " Freq:" << _pwm_frequency << "Hz");

            // Store instance:
            auto instances         = PwmChannelResources();
            instances[pwmChannel_] = this;
        }

        uint32_t getFrequency() const override { return frequency_; }
        uint32_t getMaxDuty() const override { return maxDuty_; }

        void setValue(float value) {
            if (value < 0) {
                value = 0;
            } else if (value > 1) {
                value = 1;
            }

            auto duty = value * (uint32_t(1) << int(resolutionBits_));
            ledcSetDuty(pwmChannel_, uint32_t(duty));
        }

        ~NativePwm() override {
            auto native = pin_.getNative(Pin::Capabilities::PWM | Pin::Capabilities::Native);

            ledcSetDuty(pwmChannel_, 0);
            ledcDetachPin(native);

            // Release resource:
            auto instances = PwmChannelResources();
            for (int i = 0; i < 16; ++i) {
                if (instances[i] == this) {
                    instances[i] = nullptr;
                }
            }
        }
    };

    PwmPin::PwmPin(Pin&& pin, uint32_t frequency, uint32_t maxDuty) {
        Assert(pin.capabilities().has(Pin::Capabilities::PWM | Pin::Capabilities::Native), "Pin does not support PWM");

        // For now, we only support Native pins. In the future, we might support other pins.
        _detail = new NativePwm(std::move(pin), frequency, maxDuty);
    }
}
