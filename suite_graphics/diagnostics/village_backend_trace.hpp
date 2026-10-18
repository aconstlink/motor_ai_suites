#pragma once

// Temporary, single-D3D-window diagnostic. No per-draw probes, waits or logging.
#include <motor/std/vector>
#include <motor/log/global.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <windows.h>

namespace village_diagnostics
{
    using namespace motor::core::types ;
    using stamp_t = long long ;

    class carrier_trace
    {
        struct record
        {
            stamp_t previous_end, start, end ;
            double setup, begin, execute, finish, present, deactivate ;
            double miss_sleep, outer_sleep, maintenance ;
            size_t miss_count, outer_count, empty_polls ;
        } ;
        char _path[4096] = {} ;
        motor::vector<record> _records ;
        stamp_t _previous_end = 0 ;
        double _miss_sleep = 0, _outer_sleep = 0, _maintenance = 0 ;
        size_t _miss_count = 0, _outer_count = 0, _empty_polls = 0, _dropped = 0 ;
        bool_t _enabled = false ;

    public:
        carrier_trace( void_t )
        {
            auto const n = GetEnvironmentVariableA("MOTOR_VILLAGE_BACKEND_TRACE",_path,DWORD(sizeof(_path))) ;
            _enabled = n > 0 && n < sizeof(_path) ;
            if( _enabled ) _records.reserve(32768) ;
        }

        stamp_t stamp( void_t ) const noexcept
        {
            return _enabled ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() : 0 ;
        }

        void_t miss( void_t ) noexcept { if( _enabled ) ++_empty_polls ; }
        void_t sleep( stamp_t const start, bool_t const backoff ) noexcept
        {
            if( !_enabled ) return ;
            double const duration = double(stamp()-start)/1000000.0 ;
            if( backoff ) { _miss_sleep += duration ; ++_miss_count ; }
            else { _outer_sleep += duration ; ++_outer_count ; }
        }
        void_t maintenance( stamp_t const start ) noexcept
        {
            if( _enabled ) _maintenance += double(stamp()-start)/1000000.0 ;
        }
        void_t frame( stamp_t const start, stamp_t const setup, stamp_t const begin,
            stamp_t const execute, stamp_t const finish, stamp_t const present, stamp_t const end )
        {
            if( !_enabled ) return ;
            if( _records.size() < _records.capacity() )
                _records.push_back({_previous_end,start,end,double(setup-start)/1000000.0,
                    double(begin-setup)/1000000.0,double(execute-begin)/1000000.0,
                    double(finish-execute)/1000000.0,double(present-finish)/1000000.0,
                    double(end-present)/1000000.0,_miss_sleep,_outer_sleep,_maintenance,
                    _miss_count,_outer_count,_empty_polls}) ;
            else ++_dropped ;
            _previous_end = end ; _miss_sleep = 0 ; _outer_sleep = 0 ; _maintenance = 0 ;
            _miss_count = 0 ; _outer_count = 0 ; _empty_polls = 0 ;
        }

        ~carrier_trace( void_t )
        {
            if( !_enabled ) return ;
            std::ofstream out(_path) ; out.imbue(std::locale::classic()) ;
            out << "previous_end_ns,start_ns,end_ns,gap_ms,setup_ms,begin_ms,execute_ms,end_ms,present_ms,deactivate_ms,miss_sleep_ms,outer_sleep_ms,maintenance_ms,miss_sleep_count,outer_sleep_count,empty_polls\n" ;
            out << std::fixed << std::setprecision(6) ;
            for( auto const & r : _records )
                out << r.previous_end << ',' << r.start << ',' << r.end << ','
                    << (r.previous_end ? double(r.start-r.previous_end)/1000000.0 : 0) << ','
                    << r.setup << ',' << r.begin << ',' << r.execute << ',' << r.finish << ','
                    << r.present << ',' << r.deactivate << ',' << r.miss_sleep << ','
                    << r.outer_sleep << ',' << r.maintenance << ',' << r.miss_count << ','
                    << r.outer_count << ',' << r.empty_polls << '\n' ;
            out.flush() ;
            motor::log::global_t::test("[village diagnostics] backend trace written without dropped records",out.good() && _dropped == 0) ;
        }
    } ;
}
