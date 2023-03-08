// Copyright (c) 2021 -  Stefan de Bruijn
// Copyright (c) 2021 -  Mitch Bradley
// Copyright (c) 2023 -  Vlad A.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "src/Configuration/Configurable.h"
//#include "src/System.h"    // AxisMask, MotorMask
//#include "src/Protocol.h"  // ExecAlarm
//#include <queue>

namespace Kinematics {
    class SkewAxis  : public Configuration::Configurable {
        uint _axisIdx;
    public:
        SkewAxis( int currentAxis ) : _axisIdx( currentAxis ) { _x[5] = _x[4] = _x[3] = _x[2] = _x[1] = _x[0] = 0.0f; };

        float    _dist     = 10.0f;
        float    _x[ 6 ];

        // Configuration system helpers:
        void validate() override {};
        void afterParse() override;
        void group(Configuration::HandlerBase& handler) override;
        void init();

        void getRow( const uint count, float* buf );
    };

    class Skew : public Configuration::Configurable {
        static constexpr const char* _names = "xyzabc";
        uint _numberAxis;

        template< typename number >
        class Mtx {
            uint    _pitch;
            uint    _lines;
            number* _buffer;

        public:
            Mtx( const uint row, const uint col ) : _pitch( col ), _lines( row )  {
                _buffer = nullptr; };

            Mtx( number* extBuffer, const uint row, const uint col ) : _pitch( col ), _lines( row )  {
                _buffer = extBuffer; };

            void allocate() { _buffer = new number[ _pitch * _lines ]; }
            void deallocate() { if ( _buffer ) delete[] _buffer; }

            number* getBuffer() { return _buffer; }
            number  value( const uint row, const uint col ) const { return _buffer[ row * _pitch + col ]; }
            number* ptr( const uint row, const uint col ) { return _buffer + row * _pitch + col; }
            void    transform( const number* from, number* to ) const;

            void dumpRow( const uint idx ) const;
            void dump() const;

            ~Mtx() {}
        };
        using MtxF = Mtx< float >;
        using MtxD = Mtx< double >;

    public:
        Skew();

        SkewAxis* _axis[6];

        void init();
        void group(Configuration::HandlerBase& handler) override;
        void afterParse() override;
        void validate() override;

        void txAxes(  float* to, const float* from ) const { if ( _mtx ) _mtx->transform( from, to ); }
        void revAxes( float* to, const float* from ) const { if ( _rev ) _rev->transform( from, to ); }
        bool isValid() const { return ( _rev != nullptr ); }

        ~Skew();
    private:
        MtxF* _mtx;
        MtxF* _rev;

        bool GJ_invertMatrix( const uint size, const MtxF* const A, MtxF* const B );
    };
}
 