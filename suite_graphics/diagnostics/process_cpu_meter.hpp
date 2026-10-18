#pragma once

#include <motor/log/global.h>
#include <chrono>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace village_diagnostics
{
    class process_cpu_meter
    {
        using void_t = motor::core::types::void_t ;
        std::chrono::steady_clock::time_point _start ;
        double _cpu_start = 0 ;
        bool _valid = false ;

        static bool read( double & milliseconds ) noexcept
        {
#if defined(_WIN32)
            FILETIME creation, exit, kernel, user ;
            if( !GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user) ) return false ;
            ULARGE_INTEGER k, u ;
            k.LowPart = kernel.dwLowDateTime ; k.HighPart = kernel.dwHighDateTime ;
            u.LowPart = user.dwLowDateTime ; u.HighPart = user.dwHighDateTime ;
            milliseconds = double(k.QuadPart+u.QuadPart)/10000.0 ;
            return true ;
#else
            (void)milliseconds ; return false ;
#endif
        }

    public:
        void_t start( void_t ) noexcept
        {
            _valid = read(_cpu_start) ;
            _start = std::chrono::steady_clock::now() ;
        }

        void_t finish( void_t ) noexcept
        {
#if defined(_WIN32)
            auto const end = std::chrono::steady_clock::now() ;
            double cpu_end = 0 ;
            if( !_valid || !read(cpu_end) )
            {
                motor::log::global_t::test("[village benchmark] process CPU timing",false) ;
                return ;
            }
            double const wall = std::chrono::duration<double,std::milli>(end-_start).count() ;
            double const cpu = cpu_end-_cpu_start ;
            DWORD const processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) ;
            if( wall <= 0 || processors == 0 ) return ;
            motor::log::global_t::status<512>(
                "VILLAGE_CPU wall_ms=%.4f cpu_ms=%.4f logical_processors=%lu core_equivalents=%.4f machine_percent=%.4f",
                wall,cpu,static_cast<unsigned long>(processors),cpu/wall,100.0*cpu/wall/double(processors)) ;
#endif
        }
    } ;
}
