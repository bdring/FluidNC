// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "I2CPinExtenderBase.h"

#include <bitset>

namespace Extenders {
    class PCA9539 : public I2CPinExtenderBase {
    public:
        PCA9539(const char* name);

        const char* name() const override;
    };
}
