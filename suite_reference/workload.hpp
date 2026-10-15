#pragma once

#include <motor/log/global.h>
#include <motor/math/utility/3d/transformation.hpp>
#include <motor/std/string>
#include <motor/std/vector>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace reference
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;
    using matrix_t = std::array<float_t, 16> ; // Column-major upload layout.

    struct options
    {
        size_t objects = 1000, warmup = 120, frames = 300 ;
        motor::string_t backend = "gl", capture ;
    } ;

    inline void_t usage( void_t )
    {
        motor::log::global_t::status( "--objects 1..5000 --warmup 2..10000 --frames 10..10000 [--backend gl|d3d (bgfx only)] [--capture path.ppm]" ) ;
    }

    inline bool_t parse( int argc, char ** argv, options & opts, bool_t const allow_d3d = false )
    {
        for( int i = 1 ; i < argc ; ++i )
        {
            motor::string_t const name = argv[i] ;
            if( ++i == argc ) return false ;
            if( name == "--backend" ) { opts.backend = argv[i] ; continue ; }
            if( name == "--capture" )
            {
                opts.capture = argv[i] ;
                if( opts.capture.empty() ) return false ;
                continue ;
            }
            size_t value = 0 ;
            auto const end = argv[i] + std::strlen(argv[i]) ;
            auto const parsed = std::from_chars( argv[i], end, value ) ;
            if( parsed.ec != std::errc() || parsed.ptr != end ) return false ;
            if( name == "--objects" ) opts.objects = value ;
            else if( name == "--warmup" ) opts.warmup = value ;
            else if( name == "--frames" ) opts.frames = value ;
            else return false ;
        }
        return opts.objects > 0 && opts.objects <= 5000 && opts.warmup >= 2 && opts.warmup <= 10000 &&
            opts.frames >= 10 && opts.frames <= 10000 &&
            (opts.backend == "gl" || (allow_d3d && opts.backend == "d3d")) ;
    }

    struct vertex { float_t position[3], normal[3] ; } ;
    inline constexpr vertex vertices[] = {
        {{-1,-1, 1},{0,0,1}}, {{ 1,-1, 1},{0,0,1}}, {{ 1, 1, 1},{0,0,1}}, {{-1, 1, 1},{0,0,1}},
        {{ 1,-1,-1},{0,0,-1}},{{-1,-1,-1},{0,0,-1}},{{-1, 1,-1},{0,0,-1}},{{ 1, 1,-1},{0,0,-1}},
        {{ 1,-1, 1},{1,0,0}}, {{ 1,-1,-1},{1,0,0}}, {{ 1, 1,-1},{1,0,0}}, {{ 1, 1, 1},{1,0,0}},
        {{-1,-1,-1},{-1,0,0}},{{-1,-1, 1},{-1,0,0}},{{-1, 1, 1},{-1,0,0}},{{-1, 1,-1},{-1,0,0}},
        {{-1, 1, 1},{0,1,0}}, {{ 1, 1, 1},{0,1,0}}, {{ 1, 1,-1},{0,1,0}}, {{-1, 1,-1},{0,1,0}},
        {{-1,-1,-1},{0,-1,0}},{{ 1,-1,-1},{0,-1,0}},{{ 1,-1, 1},{0,-1,0}},{{-1,-1, 1},{0,-1,0}}
    } ;
    inline constexpr uint32_t indices[] = {
        0,1,2,0,2,3, 4,5,6,4,6,7, 8,9,10,8,10,11,
        12,13,14,12,14,15, 16,17,18,16,18,19, 20,21,22,20,22,23
    } ;

    struct object_data
    {
        motor::math::vec3f_t position ;
        float_t scale ;
        std::array<float_t, 4> color ;
        matrix_t world {} ;
    } ;

    inline motor::vector<object_data> make_objects( size_t const count )
    {
        motor::vector<object_data> objects ;
        objects.reserve(count) ;
        size_t const columns = size_t(std::ceil(std::sqrt(double_t(count) * 16.0 / 9.0))) ;
        size_t const rows = (count + columns - 1) / columns ;
        float_t const spacing = 22.0f / float_t(columns) ;
        for( size_t i = 0 ; i < count ; ++i )
        {
            objects.push_back( { motor::math::vec3f_t(
                (float_t(i % columns) - float_t(columns-1) * 0.5f) * spacing,
                (float_t(i / columns) - float_t(rows-1) * 0.5f) * spacing, 0.0f),
                spacing * 0.27f, {0.3f + 0.6f * float_t(i%3) / 2.0f, 0.65f, 0.9f, 1.0f}, {} } ) ;
        }
        return objects ;
    }

    inline void_t update( motor::vector<object_data> & objects, size_t const tick )
    {
        float_t const angle = float_t(tick % 360) * 0.01745329252f ;
        for( auto & object : objects )
        {
            motor::math::m3d::trafof_t const transform( motor::math::vec3f_t(object.scale),
                motor::math::vec3f_t(0.3f, angle, 0.0f), object.position ) ;
            auto const m = transform.get_transformation() ;
            for( size_t row = 0 ; row < 4 ; ++row )
                for( size_t col = 0 ; col < 4 ; ++col ) object.world[col*4+row] = m[row*4+col] ;
        }
    }

    inline matrix_t view( void_t ) { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-30,1} ; }
    inline matrix_t projection( bool_t const homogeneous_depth = true )
    {
        return {2.0f/24.0f,0,0,0, 0,2.0f/13.5f,0,0,
            0,0,(homogeneous_depth ? -2.0f : -1.0f)/99.0f,0,
            0,0,(homogeneous_depth ? -101.0f : -1.0f)/99.0f,1} ;
    }

    inline double_t milliseconds( clock_t::time_point a, clock_t::time_point b )
    { return std::chrono::duration<double_t, std::milli>(b-a).count() ; }

    struct sample { double_t frame = 0, update = 0, submit = 0 ; } ;
    inline double_t median( motor::vector<double_t> & data )
    {
        std::sort(data.begin(), data.end()) ;
        size_t const n = data.size() ;
        return n % 2 ? data[n/2] : (data[n/2-1]+data[n/2])*0.5 ;
    }

    class measurement
    {
        options _opts ;
        clock_t::time_point _started = clock_t::now(), _previous = _started ;
        motor::vector<sample> _samples ;
    public:
        size_t tick = 0 ;
        sample current ;
        explicit measurement( options const & opts ) : _opts(opts) { _samples.reserve(opts.frames) ; }
        bool_t begin( clock_t::time_point const now )
        {
            current.frame = milliseconds(_previous, now) ;
            if( tick > _opts.warmup ) _samples.push_back(current) ;
            _previous = now ;
            return !complete() && milliseconds(_started, now) < 300000.0 ;
        }
        bool_t complete( void_t ) const { return _samples.size() == _opts.frames ; }
        void_t report( char const * engine ) const
        {
            motor::vector<double_t> frames, updates, submits ;
            for( auto const & s : _samples )
            { frames.push_back(s.frame) ; updates.push_back(s.update) ; submits.push_back(s.submit) ; }
            auto const f = median(frames), u = median(updates), s = median(submits) ;
            size_t const p = size_t(std::ceil(double_t(frames.size()) * 0.95)) - 1 ;
            motor::log::global_t::status( "REFERENCE_BENCH_HEADER,engine,backend,objects,samples,draws_per_frame,frame_median_ms,frame_p95_ms,update_median_ms,submit_median_ms" ) ;
            motor::log::global_t::status<512>( "REFERENCE_BENCH,%s,%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f",
                engine, _opts.backend.c_str(), _opts.objects, _samples.size(), _opts.objects, f, frames[p], u, s ) ;
        }
    } ;

    inline bool_t write_ppm( char const * path, uint32_t w, uint32_t h,
        uint32_t pitch, uint8_t const * pixels, bool_t flip, bool_t bgra )
    {
        std::ofstream out(path, std::ios::binary) ;
        out << "P6\n" << w << ' ' << h << "\n255\n" ;
        size_t lit = 0 ;
        for( uint32_t y = 0 ; y < h ; ++y )
        {
            auto const * row = pixels + size_t(flip ? h-1-y : y) * pitch ;
            for( uint32_t x = 0 ; x < w ; ++x )
            {
                auto const * p = row + x*4 ;
                char const rgb[] = {char(p[bgra ? 2 : 0]), char(p[1]), char(p[bgra ? 0 : 2])} ;
                out.write(rgb, 3) ;
                if( p[0] || p[1] || p[2] ) ++lit ;
            }
        }
        motor::log::global_t::status<256>( "REFERENCE_CAPTURE,%u,%u,foreground_pixels,%zu", w, h, lit ) ;
        return out.good() && lit > 0 && lit < size_t(w)*h ;
    }
}
