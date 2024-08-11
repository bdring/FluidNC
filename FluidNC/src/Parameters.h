// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <string>

bool assign_param(const char* line, size_t* pos);
bool read_number(const char* line, size_t* pos, float& value, bool in_expression = false);
void perform_assignments();
bool named_param_exists(std::string& name);
void set_named_param(const char* name, float value);
