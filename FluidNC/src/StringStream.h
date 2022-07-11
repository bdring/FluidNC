// Copyright (c) 2021 Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Print.h"

class StringStream : public Print {
    String _data;

public:
    size_t write(uint8_t c) override {
        _data += char(c);
        return 1;
    }

    String str() { return _data; }
    //    StringRange str() const { return StringRange(data_.data(), data_.data() + data_.size()); }
};
