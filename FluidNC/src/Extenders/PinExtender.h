// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "../Configuration/Configurable.h"
#include "PinExtenderDriver.h"

namespace Extenders {
    class PinExtender : public Configuration::Configurable {
    public:
        // Other configurations?
        PinExtenderDriver* _driver;

        PinExtender();

        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;
        void init();

        ~PinExtender();
    };
}
