// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#if MAX_N_I2c
#    include "Extenders.h"
#    include "PCA9535_9555.h"

namespace Extenders {
    PCA9535_9555::PCA9535_9555(const char* name) : I2CPinExtenderBase(name) {
        _baseAddress = 0x20;
    }

    const char* PCA9535_9555::name() const {
        return "pca9535_9555";
    }

    // Register extender:
    namespace {
        PinExtenderFactory::InstanceBuilder<PCA9535_9555> registration("pca9535_9555");
    }
}
#endif
