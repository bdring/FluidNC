#include <cstdint>

extern uint32_t ticks_per_us;

void timing_init();
void spinUntil(int32_t endTicks);
void delay_us(int32_t us);

int32_t usToCpuTicks(int32_t us);
int32_t usToEndTicks(int32_t us);
int32_t getCpuTicks();
