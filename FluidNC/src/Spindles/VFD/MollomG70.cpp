#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class MollomG70 : public GenericProtocol {
        public:
            MollomG70() :
                GenericProtocol("MollomG70",
                0xffffffff,                              // min_rpm
                0xffffffff,                              // max_rpm
                "06 20 00 00 01 > echo",                 // cw
                "06 20 00 00 02 > echo",                 // ccw
                "06 20 00 00 06 > echo",                 // off
                "06 10 00 rpm%*100 > echo",              // set_rpm
                "03 70 00 00 01 > 03 02 rpm*60/100",     // get_rpm
                "03 f0 0e 00 01 > 03 02 minrpm*60/100",  // get_min_rpm
                "03 f0 0c 00 01 > 03 02 maxrpm*60/100"   // get_max_rpm
            ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, MollomG70> registration("MollomG70");
        }
    }
}
