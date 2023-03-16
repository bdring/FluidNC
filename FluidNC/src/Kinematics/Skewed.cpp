#include "Skewed.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {

    void SkewAxis::group(Configuration::HandlerBase& handler) {
        handler.item("dist", _dist, 1.0f, 100000.0f );
        handler.item("x", _x[0], -1000.0f, 1000.0f );
        handler.item("y", _x[1], -1000.0f, 1000.0f );
        handler.item("z", _x[2], -1000.0f, 1000.0f );
        handler.item("a", _x[3], -1000.0f, 1000.0f );
        handler.item("b", _x[4], -1000.0f, 1000.0f );
        handler.item("c", _x[5], -1000.0f, 1000.0f );
    }

    void SkewAxis::init() {
        log_info( "      Skew ( " << _x[0] << ", " << _x[1] << ", " << _x[2] << " ) over " << _dist << "mm" );
    }


    // This return a row of transformation matrix 
    void SkewAxis::getRow( const uint count, float* buf ) {
        for( uint i = 0; i < count; ++i )
            buf[ i ] = _x[ i ] / _dist + (( i == _axisIdx ) ? 1.0f : 0.0 );
    }

    ////////////////////

    Skewed::Skewed() : _numberAxis( MAX_N_AXIS ), _axis() {
        for (int i = 0; i < MAX_N_AXIS; ++i) {
            _axis[i] = nullptr;
        }

    }

    void Skewed::init() {
        GenericCartesian::init();

        bool fail = false;

        if (_mtx)
            delete _mtx;

        if (_rev)
            delete _rev;

        _mtx = new Mtx<float>(_numberAxis, _numberAxis);
        _rev = new Mtx<float>(_numberAxis, _numberAxis);

        for (size_t axis = 0; axis < _numberAxis; axis++) {
            auto a = _axis[axis];
            if (a) {
                a->init();
                a->getRow( _numberAxis, _mtx->ptr( axis, 0 ) );
            } else {
                fail = true;
                break;
            }
        }

        if (!fail)
            fail = ! GJ_invertMatrix(_numberAxis, _mtx, _rev);

        if (!fail) {
#ifdef LOG_MATRIX_CONTENT
            log_info("Direct transform");
            _mtx->dump();
            log_info("Reverse transform");
            _rev->dump();
#endif
        } else {
            log_warn("Fail during building transformation matrices. Probably skew settings are too wild. Skew correction will be "
                     "disabled.");
            
            if (_mtx) {
                delete _mtx;
                _mtx = nullptr;
            }

            if (_rev) {
                delete _rev;
                _rev = nullptr;
            }
        }
    }

    void Skewed::afterParse() {
        // Find the last axis that was declared and set _numberAxis accordingly
        for (size_t i = MAX_N_AXIS; i > 0; --i) {
            if (_axis[i - 1] != nullptr) {
                _numberAxis = i;
                break;
            }
        }
        // Senders might assume 3 axes in reports
        if (_numberAxis < 3) {
            _numberAxis = 3;
        }

        for (size_t i = 0; i < _numberAxis; ++i) {
            if (_axis[i] == nullptr) {
                _axis[i] = new SkewAxis(i);
            }
        }
    }

    void Skewed::group(Configuration::HandlerBase& handler) {
        char   tmp[2];
        size_t n_axis = _numberAxis ? _numberAxis : MAX_N_AXIS;

        for (size_t i = 0; i < n_axis; ++i) {
            tmp[0] = _names[i];
            tmp[1] = 0;

            handler.section(tmp, _axis[i], i);
        }
    }

    void Skewed::validate() {
        log_info("validation for Skewed");
        init();
        log_info("validation is done")
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Skewed> registration("Skewed");
    }
}
