#pragma once

#include "JSONEncoder.h"
#include "Channel.h"

void platform_sys_stats(JSONencoder& j);
void platform_sys_stats(Channel& out);
