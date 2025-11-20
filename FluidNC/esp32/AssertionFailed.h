// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include <stdexcept>

class AssertionFailed {
public:
    static std::runtime_error create(const char* msg, ...);
};
