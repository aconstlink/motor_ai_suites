#pragma once
#include <motor/concurrent/typedefs.h>
#include <motor/log/global.h>
#include <motor/std/string>
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>

namespace cost_test
{
    using namespace motor::core::types ;
    using clock = std::chrono::steady_clock ;
    class watchdog
    {
        std::mutex _mutex ;
        std::condition_variable _cv ;
        bool_t _done = false ;
        motor::concurrent::thread_t _thread ;
    public:
        watchdog( void_t ) : _thread( [this]( void_t )
        {
            std::unique_lock<std::mutex> lock( _mutex ) ;
            if( _cv.wait_for( lock, std::chrono::seconds( 60 ), [this]( void_t ) { return _done ; } ) ) return ;
            motor::log::global_t::test( "[cost] watchdog", false ) ;
            std::_Exit( EXIT_FAILURE ) ;
        } ) {}
        ~watchdog( void_t )
        {
            { std::lock_guard<std::mutex> lock( _mutex ) ; _done = true ; }
            _cv.notify_one() ; _thread.join() ;
        }
    } ;

    inline double_t elapsed_ms( clock::time_point const start )
    { return std::chrono::duration<double_t, std::milli>( clock::now() - start ).count() ; }

    inline void_t report( motor::string_in_t name, std::array<double_t, 3> times,
        size_t const repetitions, size_t const operations )
    {
        std::sort( times.begin(), times.begin() + repetitions ) ;
        double_t const median = times[repetitions / 2] ;
        motor::log::global_t::status( name + " median_ms=" + motor::to_string( median ) +
            " min_ms=" + motor::to_string( times[0] ) + " max_ms=" + motor::to_string( times[repetitions-1] ) +
            " ns/op=" + motor::to_string( median * 1000000.0 / double_t( operations ) ) ) ;
    }
}

#if defined(_MSC_VER)
#define COST_NOINLINE __declspec(noinline)
#else
#define COST_NOINLINE __attribute__((noinline))
#endif
