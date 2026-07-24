#pragma once

#include <cstdint>

namespace MotorDrivers {
    constexpr float tmc2209EffectiveRSense(float configured, float default_value) {
        return configured == 0.0f ? default_value : configured;
    }

    struct TMC2209UartEndpoint {
        int32_t uart_num;
        uint8_t addr;
        bool    has_selector;

        constexpr bool sharesUnselectedAddress(const TMC2209UartEndpoint& other) const {
            return uart_num == other.uart_num && addr == other.addr && !has_selector && !other.has_selector;
        }
    };

    constexpr bool tmc2209RequiresReadback(bool shared_address_write_only) { return !shared_address_write_only; }

    // UART-controlled settings that must be identical when several TMC2209s
    // intentionally share a hardware address.  Motion and limit pins are
    // deliberately absent: they remain independent per motor.
    struct TMC2209UartSettings {
        float    r_sense;
        float    run_current;
        float    hold_current;
        float    homing_current;
        int32_t  microsteps;
        uint32_t run_mode;
        uint32_t homing_mode;
        int32_t  stallguard;
        uint8_t  toff_disable;
        uint8_t  toff_stealthchop;
        uint8_t  toff_coolstep;
        bool     use_enable;

        const char* mismatch(const TMC2209UartSettings& other) const {
#define TMC2209_COMPARE(field) \
    if (field != other.field)  \
        return #field
            TMC2209_COMPARE(r_sense);
            TMC2209_COMPARE(run_current);
            TMC2209_COMPARE(hold_current);
            TMC2209_COMPARE(homing_current);
            TMC2209_COMPARE(microsteps);
            TMC2209_COMPARE(run_mode);
            TMC2209_COMPARE(homing_mode);
            TMC2209_COMPARE(stallguard);
            TMC2209_COMPARE(toff_disable);
            TMC2209_COMPARE(toff_stealthchop);
            TMC2209_COMPARE(toff_coolstep);
            TMC2209_COMPARE(use_enable);
#undef TMC2209_COMPARE
            return nullptr;
        }
    };
}
