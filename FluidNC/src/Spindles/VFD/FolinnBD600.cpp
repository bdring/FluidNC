#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class FolinnBD600 : public GenericProtocol {
        public:
            FolinnBD600() :
                GenericProtocol("FolinnBD600",
                                0xffffffff,                         // min_rpm
                                0xffffffff,                         // max_rpm
                                "06 10 00 00 02 > echo",            // cw
                                "06 10 00 00 01 > echo",            // ccw
                                "06 10 00 00 06 > echo",            // off
                                "06 30 00 rpm%*100 > echo",         // set_rpm
                                "03 30 01 00 01 > 03 02 rpm*3",     // get_rpm
                                "03 F0 05 00 01 > 03 02 minRPM*3",  // get_min_rpm
                                "03 F0 04 00 01 > 03 02 maxRPM*3"   // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, FolinnBD600> registration("FolinnBD600");
        }
    }
}
