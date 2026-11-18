// Copyright (c) 2021 - Stefan de Bruijn
// Copyright (c) 2026 - FluidNC contributors
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"
#if MAX_N_PARLIO
#    include "PinDetail.h"

namespace Pins {
    class PARLIOPinDetail : public PinDetail {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;

        static const int         nPARLIOPins = 16;
        static std::vector<bool> _claimed;

        bool _lastWrittenValue = false;

    public:
        PARLIOPinDetail(pinnum_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;

        // The PARLIO engine drives the whole 16-bit word, so writes to an
        // individual bus bit outside the engine are intentionally ignored.
        void          write(bool high) override;
        void          synchronousWrite(bool high) override;
        bool          read() override;
        void          setAttr(PinAttributes value, uint32_t frequency) override;
        PinAttributes getAttr() const override;

        bool canStep() override { return true; }

        ~PARLIOPinDetail() override { _claimed[_index] = false; }
    };
}
#endif
