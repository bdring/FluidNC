#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class DeltaMS300 : public GenericProtocol {
        public:
            DeltaMS300() :
                GenericProtocol("DeltaMS300",
                                2000,                                 // min_rpm
                                24000,                                // max_rpm
                                "06 20 00 00 12 > echo",              // cw
                                "06 20 00 00 22 > echo",              // ccw
                                "06 20 00 00 01 > echo",              // off
                                "06 20 01 rpm*100/60 > echo",         // set_rpm
                                "03 21 03 00 01 > 03 02 rpm*60/100",  // get_rpm
                                "",                                   // get_min_rpm
                                ""                                    // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, DeltaMS300> registration("DeltaMS300");
        }
    }
}
