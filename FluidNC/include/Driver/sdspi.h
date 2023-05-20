#include <system_error>

bool sd_init_slot(uint32_t freq_hz, int cs_pin, int cd_pin = -1, int wp_pin = -1);
void sd_unmount();
void sd_deinit_slot();

std::error_code sd_mount(int max_files = 1);
