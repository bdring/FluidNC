#include "Skew.h"

#include <cmath>
#include <cstring>

#define LOG_MATRIX_CONTENT

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

void SkewAxis::afterParse() {

}

void SkewAxis::init() {
    log_info( "      Skew ( " << _x[0] << ", " << _x[1] << ", " << _x[2] << " ) over " << _dist << "mm" );
}

/*
void SkewAxis::validate() const {
    Assert(_dist >= 0, "Skew distance must be greater then zero"); 
    log_info( "Skew for axis " << _axisIdx << " is validating")
    log_info( "Skew ( " << _x[0] << ", " << _x[1] << ", " << _x[2] << " ) over " << _dist << "mm" );
}
*/

// This return a row of transformation matrix 
void SkewAxis::getRow( const uint count, float* buf ) {
    for( uint i = 0; i < count; ++i )
        buf[ i ] = _x[ i ] / _dist + (( i == _axisIdx ) ? 1.0f : 0.0 );
}

////////////////////

Skew::Skew() : _numberAxis( MAX_N_AXIS ), _axis() {
    for (int i = 0; i < MAX_N_AXIS; ++i) {
        _axis[i] = nullptr;
    }

    _mtx = nullptr;
    _rev = nullptr;
}

void Skew::init() {

    bool fail = false;

    if ( _mtx ) {
        _mtx->deallocate();
        delete _mtx;
    }

    if ( _rev ) {
        _rev->deallocate();
        delete _rev;
    }

    _mtx = new MtxF( _numberAxis, _numberAxis );
    _rev = new MtxF( _numberAxis, _numberAxis );

    _mtx->allocate();
    _rev->allocate();

    for ( size_t axis = 0; axis < _numberAxis; axis++ ) {
        auto a = _axis[axis];
        if (a) {
            log_info("    " << _names[axis] );
            a->init();

            a->getRow( _numberAxis, _mtx->ptr( axis, 0 ) );
        }
        else {
            fail = true;
            break;
        }
    }

    if ( !fail )
        fail = !GJ_invertMatrix( _numberAxis, _mtx, _rev );

    if ( !fail ) {
    #ifdef LOG_MATRIX_CONTENT
        log_info( "Direct transform");
        _mtx->dump();
        log_info( "Reverse transform");
        _rev->dump();
    #endif
    } else {
        log_warn( "Fail during building transformation matrices. Probably skew settings are too wild. Skew correction will be disabled.")
        if ( _mtx ) {
            _mtx->deallocate();
            delete _mtx;
            _mtx = nullptr;
        }

        if ( _rev ) {
            _rev->deallocate();
            delete _rev;
            _rev = nullptr;
        }
    }

}

void Skew::validate() {
    log_info( "validation for Skew" );
    init();
    log_info( "validation is done" )
}

void Skew::group(Configuration::HandlerBase& handler) {
    char tmp[2];
    size_t n_axis = _numberAxis ? _numberAxis : MAX_N_AXIS;
    for (size_t i = 0; i < n_axis; ++i) {
        tmp[0] = _names[i];
        tmp[1] = 0;

        handler.section(tmp, _axis[i], i);
    }
}

void Skew::afterParse() {
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

Skew::~Skew() {
    if ( _mtx ) {
        _mtx->deallocate();
        delete _mtx;
    }

    if ( _rev ) {
        _rev->deallocate();
        delete _rev;
    }
}

////////////////////

template< typename number >
void Skew::Mtx< number >::dumpRow( const uint idx ) const {
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
void Skew::Mtx< number >::dump() const {
    for( uint i = 0; i < _lines; ++i ) {
        dumpRow( i );
    }
}

template< typename number >
void Skew::Mtx< number >::transform( const number* from, number* to ) const {
    for( uint j = 0; j < _pitch; ++j ) {
        number A = 0.0;
        for( uint i = 0; i < _lines; ++i )
            A += from[ i ] * value( i, j );

        to[ j ] = A;
    }
}

bool Skew::GJ_invertMatrix( const uint size, const MtxF* A, MtxF* const B ) {
    //log_info( "GJ_invertMatrix" );
    // Gauss Jordan Matrix inversion. 
    MtxD T( size, size * 2 );
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

template void Skew::Mtx< float >::transform( const float* from, float* to ) const;

}

