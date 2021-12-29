#pragma once

#include "SimpleProbe.h"

namespace Probes {
    class ToolSetter : public SimpleProbe {
        // TODO: Soft limits; we need the tool setter height to calculate the soft limit
        float _height;

    public:
        const char* name() const override;

        // Configuration handlers:
        void validate() const override;
        void group(Configuration::HandlerBase& handler) override;

        void init(TripProbe callback);
        void start_cycle();
        void stop_cycle();
        bool is_tripped();

        ~ToolSetter() override;
    };
}
