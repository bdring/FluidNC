// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>

bool assign_param(const char* line, size_t* char_counter);
bool read_number(const char* line, size_t* char_counter, float& value);
void perform_assignments();
