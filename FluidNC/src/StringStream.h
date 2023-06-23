// Copyright (c) 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Print.h"

#include <vector>

class StringStream : public Print {
    std::vector<char> data_;

public:
    size_t write(uint8_t c) override {
        data_.push_back(c);
        return 1;
    }
};
