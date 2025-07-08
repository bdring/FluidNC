#pragma once

void  flowcontrol_init(void);
Error flowcontrol(uint32_t o_label, const char* line, size_t& pos, bool& skip);
