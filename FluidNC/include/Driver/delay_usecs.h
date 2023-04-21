#include <cstdint>

void timing_init();
void spinUntil(int32_t endTicks);
void delay_us(int32_t us);

int32_t usToCpuTicks(int32_t us);
int32_t usToEndTicks(int32_t us);
int32_t getCpuTicks();
