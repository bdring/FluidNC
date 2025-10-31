// Copyright (c) 2024 - Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <string>

#include <cstdint>
// TODO - make ngc_param_id_t an enum, give names to numbered parameters where
// possible
typedef uint32_t ngc_param_id_t;

bool assign_param(const char* line, size_t& pos);
bool read_number(const char* line, size_t& pos, float& value, bool in_expression = false);
bool read_number(const std::string_view sv, float& value, bool in_expression = false);
bool perform_assignments();
bool named_param_exists(std::string& name);
bool set_named_param(const char* name, float value);
bool set_numbered_param(ngc_param_id_t, float value);
