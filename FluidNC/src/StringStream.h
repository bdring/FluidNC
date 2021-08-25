// Copyright (c) 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "SimpleOutputStream.h"
#include "StringRange.h"

#include <vector>

class StringStream : public SimpleOutputStream {
    std::vector<char> data_;

public:
    void add(char c) override { data_.push_back(c); }

    StringRange str() const { return StringRange(data_.data(), data_.data() + data_.size()); }
};
