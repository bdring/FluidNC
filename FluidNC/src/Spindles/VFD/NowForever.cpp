#include "GenericProtocol.h"

namespace Spindles {
    namespace VFD {
        class NowForever : public GenericProtocol {
        public:
            NowForever() :
                GenericProtocol("NowForever",
                                0xffffffff,                                  // min_rpm
                                0xffffffff,                                  // max_rpm
                                "10 09 00 00 01 02 00 01 > echo",            // cw
                                "10 09 00 00 01 02 00 03 > echo",            // ccw
                                "10 09 00 00 01 02 00 00 > echo",            // off
                                "10 09 01 00 01 02 rpm/6 > echo",            // set_rpm
                                "03 05 02 00 01 > 03 02 rpm%*4",             // get_rpm
                                "",                                          // get_min_rpm
                                "03 00 07 00 02 >  03 04 maxrpm*6 minrpm*6"  // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, NowForever> registration("NowForever");
        }
    }
}
