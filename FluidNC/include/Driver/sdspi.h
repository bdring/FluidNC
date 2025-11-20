#include "Driver/fluidnc_gpio.h"
#include <system_error>

bool sd_init_slot(uint32_t freq_hz, pinnum_t cs_pin, pinnum_t cd_pin = INVALID_PINNUM, pinnum_t wp_pin = INVALID_PINNUM);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(uint32_t max_files = 3);
