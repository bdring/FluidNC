#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class Huanyang : public GenericProtocol {
        public:
            Huanyang() :
                GenericProtocol("Huanyang",
                                0xffffffff,                                 // min_rpm
                                0xffffffff,                                 // max_rpm
                                "03 01 01 > echo",                          // cw
                                "03 01 11 > echo",                          // ccw
                                "03 01 08 > echo",                          // off
                                "05 02 rpm*100/60 > echo",                  // set_rpm
                                "04 03 01 00 00 > 04 03 01 rpm*60/100",     // get_rpm
                                "01 03 0b 00 00 > 01 03 0B minRPM*60/100",  // get_min_rpm
                                "01 03 05 00 00 > 01 03 05 maxRPM*60/100"   // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, Huanyang> registration("Huanyang");
        }
    }
}
