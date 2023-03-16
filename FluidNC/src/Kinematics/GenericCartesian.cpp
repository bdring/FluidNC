// Copyright (c) 2020 -	Bart Dring
// Copyright (c) 2023 -	Vlad A.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "GenericCartesian.h"

#include "src/Machine/MachineConfig.h"
#include "src/Machine/Axes.h"  // ambiguousLimit()
//#include "Skew.h"              // Skew, SkewAxis
#include "src/Limits.h"

namespace Kinematics {
    void GenericCartesian::init() { 
        log_info("Kinematic system: " << name());
        init_position();
    }

    // Initialize the machine position
    void GenericCartesian::init_position() {
        auto n_axis = config->_axes->_numberAxis;
        for (size_t axis = 0; axis < n_axis; axis++) {
            set_motor_steps(axis, 0);  // Set to zeros
        }
    }

    bool GenericCartesian::cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
        if ( _mtx ) {
            _mtx->transform( _buffer, target );
            return mc_move_motors( _buffer, pl_data);
        } else
            // Without skew correction motor space is the same cartesian space, so we do no transform.
            return mc_move_motors(target, pl_data);
    }

    void GenericCartesian::motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
        if ( _rev ) {        
            _rev->transform( cartesian, motors );
        }
        else
            // Without skew correction motor space is the same cartesian space, so we do no transform.
            copyAxes(cartesian, motors);
    }

    void GenericCartesian::transform_cartesian_to_motors(float* motors, float* cartesian) {
        // Without skew correction motor space is the same cartesian space, so we do no transform.
        if ( _mtx ) {
            _mtx->transform( motors, cartesian );
        }
        else
            copyAxes(motors, cartesian);
    }

    bool GenericCartesian::canHome(AxisMask axisMask) {
        if (ambiguousLimit()) {
            log_error("Ambiguous limit switch touching. Manually clear all switches");
            return false;
        }
        return true;
    }

    bool GenericCartesian::limitReached(AxisMask& axisMask, MotorMask& motorMask, MotorMask limited) {
        // For Cartesian, the limit switches are associated with individual motors, since
        // an axis can have dual motors each with its own limit switch.  We clear the motors in
        // the mask whose limits have been reached.
        clear_bits(motorMask, limited);

        auto oldAxisMask = axisMask;

        // Set axisMask according to the motors that are still running.
        axisMask = Machine::Axes::motors_to_axes(motorMask);

        // Return true when an axis drops out of the mask, causing replan
        // on any remaining axes.
        return axisMask != oldAxisMask;
    }

    void GenericCartesian::releaseMotors(AxisMask axisMask, MotorMask motors) {
        auto axes   = config->_axes;
        auto n_axis = axes->_numberAxis;
        for (int axis = 0; axis < n_axis; axis++) {
            if (bitnum_is_true(axisMask, axis)) {
                auto paxis = axes->_axis[axis];
                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 0))) {
                    paxis->_motors[0]->unlimit();
                }
                if (bitnum_is_true(motors, Machine::Axes::motor_bit(axis, 1))) {
                    paxis->_motors[1]->unlimit();
                }
            }
        }
    }

    GenericCartesian::~GenericCartesian() {
        if (_mtx)
            delete _mtx;

        if (_rev)
            delete _rev;
    }

    ////////////////////

    template< typename number >
    void GenericCartesian::Mtx< number >::dumpRow( const uint idx ) const {
        // Prints a row of the matrix into log.
        // Useful for debuging.
        char line[256];
        char* C = line;

        for( uint i = 0; i < _pitch; ++i ) {
            number V = value( idx, i );
            if ( V >= 0.0 )
                sprintf( C, " %4.4f ", V );
            else
                sprintf( C, "%4.4f ", V );

            C = C + strlen( C );
        }

        log_info( line );
    }

    template< typename number >
    void GenericCartesian::Mtx< number >::dump() const {
        for( uint i = 0; i < _lines; ++i ) {
            dumpRow( i );
        }
    }

    template< typename number >
    void GenericCartesian::Mtx< number >::transform(  number* to, const number* from ) const {
        for( uint j = 0; j < _pitch; ++j ) {
            number A = 0.0;
            for( uint i = 0; i < _lines; ++i )
                A += from[ i ] * value( i, j );

            to[ j ] = A;
        }
    }

    bool GenericCartesian::GJ_invertMatrix( const uint size, const Mtx<float>* A, Mtx<float>* const B ) {
        //log_info( "GJ_invertMatrix" );
        // Gauss Jordan Matrix inversion. 
        Mtx<double> T( size, size * 2 );
        uint i,j,k;

        T.allocate();
        
        for( i = 0; i < size; ++i ) {
            for( j = 0; j < size; ++j ) {
                *T.ptr( i, j )        = A->value( i, j );
                *T.ptr( i, j + size ) = ( i == j ) ? 1.0 : 0.0;
            }
        }

        //T.dump();
        for( i = 0; i < size; ++i ) {
            if ( T.value( i,i ) == 0 ) {
                T.deallocate();
                return false;
            }

            for( j = 0; j < size; ++j ) {
                if ( i != j ) {
                    double S = T.value( j, i ) / T.value( i, i );
                    for( k = 0; k < size*2; ++k )
                        *T.ptr( j, k ) = T.value( j, k ) - S * T.value( i, k );
                    
                }
            }
        }

        //log_info( "After elimination" );
        //T.dump();    
        for( i = 0; i < size; ++i ) {
            for( j = 0; j < size; ++j ) 
                *B->ptr( i, j ) = static_cast< float >( T.value( i, j + size ) / T.value( i,i ) );
        }

        T.deallocate();
        return true;
    }

    template void GenericCartesian::Mtx< float >::transform( float* to, const float* from  ) const;
    template void GenericCartesian::Mtx< float >::dump() const;

}
