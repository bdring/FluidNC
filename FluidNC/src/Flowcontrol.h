#pragma once

void  flowcontrol_init(void);
Error flowcontrol(uint32_t o_label, char* line, uint_fast8_t* pos, bool& skip);
