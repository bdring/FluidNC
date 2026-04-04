#include "Driver/i2s_out.h"

#include "Stage1HostSupport.h"

uint8_t g_i2soLevels[I2S_OUT_NUM_BITS] {};
int     g_i2soWriteCalls[I2S_OUT_NUM_BITS] {};
int     g_i2soDelayCalls = 0;

void i2s_out_init(i2s_out_init_t* params) {
    Stage1HostSupport::g_i2so.called = true;
    Stage1HostSupport::g_i2so.params = *params;
}
uint8_t i2s_out_read(pinnum_t pin) {
    return g_i2soLevels[pin];
}
void i2s_out_write(pinnum_t pin, uint8_t val) {
    g_i2soLevels[pin] = val;
    ++g_i2soWriteCalls[pin];
}
void i2s_out_delay() {
    ++g_i2soDelayCalls;
}
