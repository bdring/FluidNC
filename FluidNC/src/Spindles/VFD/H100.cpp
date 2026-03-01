#include "ModbusVFD.h"

namespace Spindles {
    namespace VFD {
        class H100 : public ModbusVFD {
        public:
            H100() :
                ModbusVFD("H100",
                          0xffffffff,                                 // min_rpm
                          0xffffffff,                                 // max_rpm
                          "05 00 49 ff 00 > echo",                    // cw
                          "05 00 4A ff 00 > echo",                    // ccw
                          "05 00 4B ff 00 > echo",                    // off
                          "06 02 01 rpm*10/60 > echo",                // set_rpm
                          "04 00 00 00 02 > 04 04 rpm*60/10 ignore",  // get_rpm
                          "03 00 0B 00 01 > 03 02 minrpm*60/10",      // get_min_rpm
                          "03 00 05 00 01 > 03 02 maxrpm*60/10"       // get_max_rpm
                ) {}
        };
        namespace {
            SpindleFactory::DependentInstanceBuilder<VFDSpindle, H100> registration("H100");
        }
    }
}
