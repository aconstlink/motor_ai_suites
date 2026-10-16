#pragma once

#include <motor/log/global.h>
#include <motor/std/string>
#include <motor/std/vector>
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace compile_cost
{
    using namespace motor::core::types ;
    using clock = std::chrono::steady_clock ;
    constexpr size_t idle_frames = 90 ;
    constexpr size_t rounds = 6 ;
    constexpr size_t operations = 128 ;

    inline double_t ms( clock::time_point const a, clock::time_point const b )
    { return std::chrono::duration<double_t, std::milli>( b - a ).count() ; }

    // Same dependent arithmetic in GLSL and msl. Constants change actual source,
    // not just comments. A repeated seed deliberately permits driver-cache reuse.
    inline motor::string_t body( uint32_t const seed, size_t const revision, bool_t const msl )
    {
        motor::string_t text = msl ? "vec4_t v = u_color ;\n" : "vec4 v = u_color ;\n" ;
        uint32_t value = seed + uint32_t( revision ) * 747796405u ;
        for( size_t i = 0 ; i < operations ; ++i )
        {
            value = value * 1664525u + 1013904223u ;
            text += "v = sin( v * " + motor::to_string( 0.7 + double_t( value % 2000u ) * 0.0001 ) + " + v ) ;\n" ;
        }
        text += msl ? "out.color = v * 0.5 + vec4_t( 0.5, 0.5, 0.5, 0.5 ) ;\n" :
            "finalColor = v * 0.5 + vec4( 0.5 ) ;\n" ;
        return text ;
    }

    struct measurements
    {
        motor::vector<double_t> idle, active, latency ;
        size_t progress_frames = 0 ;

        static void_t report_values( motor::string_in_t label, motor::vector<double_t> values )
        {
            if( values.empty() )
            {
                motor::log::global_t::status( "[compile-cost] " + label + " samples=0" ) ;
                return ;
            }
            std::sort( values.begin(), values.end() ) ;
            double_t sum = 0.0 ;
            size_t over16 = 0, over33 = 0 ;
            for( auto const value : values )
            { sum += value ; over16 += value > 16.667 ; over33 += value > 33.333 ; }
            auto percentile = [&]( double_t const p ) { return values[size_t( p * double_t( values.size() - 1 ) )] ; } ;
            motor::log::global_t::status( "[compile-cost] " + label +
                " samples=" + motor::to_string( values.size() ) +
                " mean_ms=" + motor::to_string( sum / double_t( values.size() ) ) +
                " p50_ms=" + motor::to_string( percentile( 0.50 ) ) +
                " p95_ms=" + motor::to_string( percentile( 0.95 ) ) +
                " p99_ms=" + motor::to_string( percentile( 0.99 ) ) +
                " max_ms=" + motor::to_string( values.back() ) +
                " over16=" + motor::to_string( over16 ) + " over33=" + motor::to_string( over33 ) ) ;
        }

        void_t report( motor::string_in_t label ) const
        {
            report_values( label + " idle-frame", idle ) ;
            report_values( label + " compile-frame", active ) ;
            report_values( label + " request-to-ready", latency ) ;
            motor::log::global_t::status( "[compile-cost] " + label +
                " progress_frames=" + motor::to_string( progress_frames ) ) ;
        }
    } ;
}
