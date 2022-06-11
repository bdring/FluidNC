#include "../../FluidNC/src/I2SOut.h"

// STUB implementation. Won't do you any good.

int i2s_out_init() {
    return 0;
}

void i2s_out_push() {}

void i2s_out_push_sample(unsigned int x) {}

int i2s_out_set_passthrough() {
    return 0;
}

int i2s_out_set_stepping() {
    return 0;
}

void i2s_out_delay() {}

int i2s_out_set_pulse_period(unsigned int x) {
    return 0;
}

int i2s_out_reset() {
    return 0;
}

i2s_out_pulser_status_t i2s_out_get_pulser_status() {
    return i2s_out_pulser_status_t::PASSTHROUGH;
}
