#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class YL620 : public GenericProtocol {
        public:
            YL620() :
                GenericProtocol("YL620",
                                0xffffffff,                                     // min_rpm
                                0xffffffff,                                     // max_rpm
                                "06 20 00 00 12 > echo",                        // cw
                                "06 20 00 00 22 > echo",                        // ccw
                                "06 20 00 00 01 > echo",                        // off
                                "06 20 01 rpm*10/60 > echo",                    // set_rpm
                                "03 20 0b 00 01 > 03 02 rpm*6",                 // get_rpm
                                "",                                             // get_min_rpm
                                "03 03 08 00 02 > 03 04 minrpm*60/10 maxrpm*6"  // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, YL620> registration("YL620");
        }
    }
}
