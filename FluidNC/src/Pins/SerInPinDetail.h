// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Patrick Horton
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once
#ifdef ESP32

#    include "PinDetail.h"

namespace Pins
{
    class SerInPinDetail : public PinDetail
    {
        PinCapabilities _capabilities;
        PinAttributes   _attributes;
        int             _readWriteMask;

        void (*m_callback)(void*) = nullptr;
        void *m_cb_arg = nullptr;

    public:

        SerInPinDetail(pinnum_t index, const PinOptionsParser& options);

        PinCapabilities capabilities() const override;
        PinAttributes getAttr() const override  { return _attributes; }

        void          write(int high) override;
        void          synchronousWrite(int high) override;
        int           read() override;
        void          setAttr(PinAttributes value) override;

        void attachInterrupt(void (*callback)(void*), void* arg, int mode) override;
        void detachInterrupt() override;
        void doFakeInterrupt()   { m_callback(m_cb_arg); }

        String toString() override;

        ~SerInPinDetail() override
        {
        }
    };
}

#endif
