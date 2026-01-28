#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class SiemensV20 : public GenericProtocol {
        public:
            SiemensV20() :
                GenericProtocol("SiemensV20",
                                0,                                        // min_rpm
                                24000,                                    // max_rpm
                                "06 00 63 0C 7F > echo",                  // cw
                                "06 00 63 04 7F > echo",                  // ccw
                                "06 00 63 0C 7E > echo",                  // off
                                "06 00 64 rpm%*16384/100 > echo",         // set_rpm
                                "03 00 6E 00 01 > 03 02 rpm%*16384/100",  // get_rpm
                                "",                                       // get_min_rpm
                                ""                                        // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, SiemensV20> registration("SiemensV20");
        }
    }
}
