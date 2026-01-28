#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class H2A : public GenericProtocol {
        public:
            H2A() :
                GenericProtocol("H2A",
                                6000,                                // min_rpm
                                0xffffffff,                          // max_rpm
                                "06 20 00 00 01 > echo",             // cw
                                "06 20 00 00 02 > echo",             // ccw
                                "06 20 00 00 06 > echo",             // off
                                "06 10 00 rpm%*100 > echo",          // set_rpm
                                "03 70 0C 00 01 > 03 00 02 rpm",     // get_rpm or "03 70 0C 00 02 > 03 00 04 rpm 00 00",
                                "",                                  // get_min_rpm
                                "03 B0 05 00 01 >  03 00 02 maxrpm"  // get_max_rpm or "03 B0 05 00 02 >  03 00 04 maxrpm 03 F6"
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, H2A> registration("H2A");
        }
    }
}
