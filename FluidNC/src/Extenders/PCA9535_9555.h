// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "I2CPinExtenderBase.h"

#include <bitset>

namespace Extenders {
    class PCA9535_9555 : public I2CPinExtenderBase {
    public:
        PCA9535_9555(const char* name);

        const char* name() const override;
    };
}
