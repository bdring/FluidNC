// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "PCA9539.h"

namespace Extenders {
    PCA9539::PCA9539() { _baseAddress = 0x74; }

    const char* PCA9539::name() const { return "pca9539"; }

    // Register extender:
    namespace {
        PinExtenderFactory::InstanceBuilder<PCA9539> registration("pca9539");
    }
}
