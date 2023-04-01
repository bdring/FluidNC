#include "Skewed.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
#include "src/Limits.h"

namespace Kinematics {

    void SkewAxis::group(Configuration::HandlerBase& handler) {
        handler.item("distance_mm", _dist, 1.0f, 100000.0f );
        handler.item("offset_x_mm", _offsets[0], -1000.0f, 1000.0f );
        handler.item("offset_y_mm", _offsets[1], -1000.0f, 1000.0f );
        handler.item("offset_z_mm", _offsets[2], -1000.0f, 1000.0f );
        handler.item("offset_a_mm", _offsets[3], -1000.0f, 1000.0f );
        handler.item("offset_b_mm", _offsets[4], -1000.0f, 1000.0f );
        handler.item("offset_c_mm", _offsets[5], -1000.0f, 1000.0f );
    }

    void SkewAxis::init() {
        log_debug( "      Skew ( " << _offsets[0] << ", " << _offsets[1] << ", " << _offsets[2] << " ) over " << _dist << "mm" );
    }


    ////////////////////

    Skewed::Skewed() : _numberSkewAxis( MAX_N_AXIS ) {
        for (int i = 0; i < MAX_N_AXIS; ++i) {
            _skewAxis[i] = nullptr;
        }

    }

    void Skewed::init() {
        GenericCartesian::init();

        bool fail = false;

        if (_mtx)
            delete _mtx;

        if (_rev)
            delete _rev;

        _mtx = new Mtx<float>( _numberSkewAxis, _numberSkewAxis );
        _rev = new Mtx<float>( _numberSkewAxis, _numberSkewAxis );

        for (size_t axis = 0; axis < _numberSkewAxis; axis++) {
            auto a = _skewAxis[axis];
            if (a) {
                a->init();
                
                float* row = _mtx->ptr( axis, 0 );
                const SkewAxis* skew = _skewAxis[axis];

                for( uint i = 0; i < _numberSkewAxis; ++i ) {
                    row[ i ]  = skew->_offsets[ i ] / skew->_dist;
                    row[ i ] += ( i == axis ) ? 1.0f : 0.0f;
                }
            } else {
                fail = true;
                break;
            }
        }

        if (!fail)
            fail = ! GJ_invertMatrix( _numberSkewAxis, _mtx, _rev );

        if (!fail) {
            log_debug("Direct transform");
            _mtx->dump();
            log_debug("Reverse transform");
            _rev->dump();
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
        // Find the last axis that was declared and set _numberSkewAxis accordingly
        for (size_t i = MAX_N_AXIS; i > 0; --i) {
            if (_skewAxis[i - 1] != nullptr) {
                _numberSkewAxis = i;
                break;
            }
        }
        // Senders might assume 3 axes in reports
        if (_numberSkewAxis < 3) {
            _numberSkewAxis = 3;
        }

        for (size_t i = 0; i < _numberSkewAxis; ++i) {
            if (_skewAxis[i] == nullptr) {
                _skewAxis[i] = new SkewAxis();
            }
        }
    }

    void Skewed::group(Configuration::HandlerBase& handler) {
        char   tmp[2];
        size_t n_axis = _numberSkewAxis ? _numberSkewAxis : MAX_N_AXIS;

        for (size_t i = 0; i < n_axis; ++i) {
            tmp[0] = config->_axes->axisName( i );
            tmp[1] = 0;

            handler.section(tmp, _skewAxis[i]);
        }
    }

    void Skewed::validate() {
        init();
    }

    // Configuration registration
    namespace {
        KinematicsFactory::InstanceBuilder<Skewed> registration("Skewed");
    }
}
