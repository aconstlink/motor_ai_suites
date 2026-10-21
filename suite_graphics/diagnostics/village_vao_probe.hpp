#pragma once

// Temporary GL4 instrumentation, activated only by the diagnostic patch.
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <motor/std/vector>
#include <motor/log/global.h>

namespace village_vao_probe
{
    using namespace motor::core::types ;
    struct probe
    {
        using clock_t = std::chrono::steady_clock ;
        struct sample { bool_t direct ; double_t ms ; size_t calls, hits, max_links ; } ;
        motor::vector<sample> samples ;
        int mode = 0 ;
        bool_t enabled = false, direct = false ;
        size_t frame = 0, active_frames = 0, calls = 0, hits = 0, max_links = 0 ;
        clock_t::time_point start ;

        probe( void_t )
        {
#if defined(_MSC_VER)
            char * value = nullptr ; size_t size = 0 ;
            _dupenv_s(&value,&size,"MOTOR_VILLAGE_VAO") ;
#else
            auto const value = std::getenv("MOTOR_VILLAGE_VAO") ;
#endif
            if( value )
            {
                enabled = true ;
                mode = std::strcmp(value,"paired") == 0 ? 2 : (std::strcmp(value,"direct") == 0 ? 1 : 0) ;
            }
#if defined(_MSC_VER)
            std::free(value) ;
#endif
            if( enabled ) samples.reserve(4096) ;
        }
        void_t begin( void_t )
        {
            direct = enabled && (mode == 1 || (mode == 2 && (frame & 1) != 0)) ;
            ++frame ; calls = hits = max_links = 0 ; start = clock_t::now() ;
        }
        void_t visit( size_t const links, bool_t const hit )
        {
            ++calls ; hits += hit ? 1 : 0 ;
            if( links > max_links ) max_links = links ;
        }
        void_t end( void_t )
        {
            auto const ms = std::chrono::duration<double_t,std::milli>(clock_t::now()-start).count() ;
            // Empty startup/teardown frames are not village frames.
            if( enabled && calls > 20 && active_frames++ >= 180 )
                samples.push_back({direct,ms,calls,hits,max_links}) ;
        }
        ~probe( void_t )
        {
            if( samples.empty() ) return ;
            motor::log::global_t::status<256>("VILLAGE_VAO_PROBE mode=%s samples=%zu warmup=180",
                mode == 2 ? "paired" : mode == 1 ? "direct" : "linear",samples.size()) ;
            for( size_t i = 0 ; i < samples.size() ; ++i )
            {
                auto const & s = samples[i] ;
                motor::log::global_t::status<256>(
                    "VILLAGE_VAO_SAMPLE index=%zu direct=%u backend_ms=%.6f calls=%zu hits=%zu max_links=%zu",
                    i,uint_t(s.direct),s.ms,s.calls,s.hits,s.max_links) ;
            }
        }
    } ;
}
