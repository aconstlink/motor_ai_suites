#include <motor/log/global.h>
#include <motor/math/utility/3d/transformation.hpp>
#include <motor/std/string>
#include <motor/std/vector>

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace this_file
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;

    struct options
    {
        size_t objects = 1000 ;
        size_t warmup = 120 ;
        size_t frames = 300 ;
        motor::string_t matrices = "combined" ;
        motor::string_t capture ;
    } ;

    struct object_data
    {
        motor::math::vec3f_t position ;
        float_t scale ;
        float_t color[4] ;
        Matrix world {} ;
    } ;

    struct sample
    {
        double_t frame = 0.0 ;
        double_t update = 0.0 ;
        double_t submit = 0.0 ;
    } ;

    double_t milliseconds( clock_t::time_point const a, clock_t::time_point const b )
    {
        return std::chrono::duration<double_t, std::milli>( b - a ).count() ;
    }

    void_t raylib_log( int level, char const * text, va_list args )
    {
        char buffer[2048] ;
        std::vsnprintf( buffer, sizeof(buffer), text, args ) ;
        if( level >= LOG_ERROR ) motor::log::global_t::error( buffer ) ;
        else if( level >= LOG_WARNING ) motor::log::global_t::warning( buffer ) ;
        else motor::log::global_t::status( buffer ) ;
    }

    void_t usage( void_t )
    {
        motor::log::global_t::status(
            "00_raylib_benchmark --objects 1..5000 --warmup 2..10000 --frames 10..10000 [--matrices combined|separate] [--capture path.png]" ) ;
    }

    bool_t parse( int argc, char ** argv, options & opts )
    {
        for( int i = 1 ; i < argc ; ++i )
        {
            motor::string_t const name = argv[i] ;
            if( ++i >= argc ) return false ;
            if( name == "--matrices" )
            {
                opts.matrices = argv[i] ;
                continue ;
            }
            if( name == "--capture" )
            {
                opts.capture = argv[i] ;
                if( opts.capture.empty() ) return false ;
                continue ;
            }
            size_t value = 0 ;
            auto const end = argv[i] + std::strlen( argv[i] ) ;
            auto const parsed = std::from_chars( argv[i], end, value ) ;
            if( parsed.ec != std::errc() || parsed.ptr != end ) return false ;
            if( name == "--objects" ) opts.objects = value ;
            else if( name == "--warmup" ) opts.warmup = value ;
            else if( name == "--frames" ) opts.frames = value ;
            else return false ;
        }
        return opts.objects > 0 && opts.objects <= 5000 && opts.warmup >= 2 &&
            opts.warmup <= 10000 && opts.frames >= 10 && opts.frames <= 10000 &&
            (opts.matrices == "combined" || opts.matrices == "separate") ;
    }

    Matrix world_matrix( object_data const & object, float_t const angle )
    {
        // Use the same transform evaluation as Motor's direct benchmark.
        motor::math::m3d::trafof_t const transform( motor::math::vec3f_t(object.scale),
            motor::math::vec3f_t(0.3f, angle, 0.0f), object.position ) ;
        auto const m = transform.get_transformation() ;
        // Raylib's named Matrix fields are declared in row order, despite m0/m4/etc.
        return Matrix { m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15] } ;
    }

    void_t report( options const & opts, motor::vector<sample> const & samples )
    {
        motor::vector<double_t> frames, updates, submits ;
        frames.reserve( samples.size() ) ;
        updates.reserve( samples.size() ) ;
        submits.reserve( samples.size() ) ;
        for( auto const & s : samples )
        {
            frames.emplace_back( s.frame ) ;
            updates.emplace_back( s.update ) ;
            submits.emplace_back( s.submit ) ;
        }
        auto const median = []( motor::vector<double_t> & values )
        {
            std::sort( values.begin(), values.end() ) ;
            size_t const n = values.size() ;
            return n % 2 ? values[n/2] : (values[n/2-1] + values[n/2]) * 0.5 ;
        } ;
        double_t const frame = median( frames ) ;
        double_t const update = median( updates ) ;
        double_t const submit = median( submits ) ;
        size_t const p95 = size_t( std::ceil( double_t(frames.size()) * 0.95 ) ) - 1 ;
        motor::log::global_t::status(
            "RAYLIB_BENCH_HEADER,backend,objects,samples,draws_per_frame,frame_median_ms,frame_p95_ms,update_median_ms,submit_median_ms,matrices" ) ;
        motor::log::global_t::status<512>( "RAYLIB_BENCH,gl,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%s",
            opts.objects, samples.size(), opts.objects, frame, frames[p95], update, submit, opts.matrices.c_str() ) ;
    }

    int run( options const & opts )
    {
        SetTraceLogCallback( raylib_log ) ;
        SetTraceLogLevel( LOG_WARNING ) ;
        motor::string_t const title = "Raylib benchmark | OpenGL | direct | unlit | " +
            motor::to_string( opts.objects ) + " objects | " + opts.matrices + " matrices" ;
        InitWindow( 1280, 720, title.c_str() ) ;
        if( !IsWindowReady() )
        {
            motor::log::global_t::error( "Raylib window creation failed." ) ;
            return 1 ;
        }
        SetTargetFPS( 0 ) ;
        ClearWindowState( FLAG_VSYNC_HINT ) ;
        rlSetClipPlanes( 1.0, 100.0 ) ;

        int result = 0 ;
        {
            char const * const vertex_combined = R"(
                #version 330 core
                in vec3 vertexPosition;
                uniform mat4 mvp;
                void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
            )" ;
            char const * const vertex_separate = R"(
                #version 330 core
                in vec3 vertexPosition;
                uniform mat4 matModel;
                uniform mat4 matView;
                uniform mat4 matProjection;
                void main() { gl_Position = matProjection * matView * matModel * vec4(vertexPosition, 1.0); }
            )" ;
            bool_t const separate = opts.matrices == "separate" ;
            auto shader = LoadShaderFromMemory( separate ? vertex_separate : vertex_combined, R"(
                #version 330 core
                uniform vec4 base_color;
                out vec4 finalColor;
                void main() { finalColor = base_color; }
            )" ) ;
            int const color_location = GetShaderLocation( shader, "base_color" ) ;
            bool_t const matrices_ok = separate ?
                shader.locs[SHADER_LOC_MATRIX_MODEL] >= 0 && shader.locs[SHADER_LOC_MATRIX_VIEW] >= 0 &&
                    shader.locs[SHADER_LOC_MATRIX_PROJECTION] >= 0 : shader.locs[SHADER_LOC_MATRIX_MVP] >= 0 ;
            bool_t const shader_ok = shader.id != 0 && shader.id != rlGetShaderIdDefault() &&
                color_location >= 0 && matrices_ok ;
            if( !shader_ok )
            {
                motor::log::global_t::error( "Raylib benchmark shader failed; refusing default-shader timings." ) ;
                UnloadShader( shader ) ;
                CloseWindow() ;
                return 1 ;
            }

            auto mesh = GenMeshCube( 2.0f, 2.0f, 2.0f ) ;
            auto material = LoadMaterialDefault() ;
            material.shader = shader ;
            if( mesh.vertexCount != 24 || mesh.triangleCount != 12 ||
                mesh.indices == nullptr || mesh.vboId == nullptr )
            {
                motor::log::global_t::error( "Unexpected cube geometry or failed mesh upload." ) ;
                UnloadMesh( mesh ) ;
                UnloadMaterial( material ) ;
                CloseWindow() ;
                return 1 ;
            }

            motor::vector<object_data> objects ;
            objects.reserve( opts.objects ) ;
            size_t const columns = size_t( std::ceil( std::sqrt( double_t(opts.objects) * 16.0 / 9.0 ) ) ) ;
            size_t const rows = (opts.objects + columns - 1) / columns ;
            float_t const spacing = 22.0f / float_t(columns) ;
            for( size_t i = 0 ; i < opts.objects ; ++i )
            {
                object_data object {} ;
                object.position = motor::math::vec3f_t(
                    (float_t(i % columns) - float_t(columns - 1) * 0.5f) * spacing,
                    (float_t(i / columns) - float_t(rows - 1) * 0.5f) * spacing, 0.0f ) ;
                object.scale = spacing * 0.27f ;
                object.color[0] = 0.3f + 0.6f * float_t(i % 3) / 2.0f ;
                object.color[1] = 0.65f ;
                object.color[2] = 0.9f ;
                object.color[3] = 1.0f ;
                objects.emplace_back( object ) ;
            }
            Camera3D const camera { {0,0,30}, {0,0,0}, {0,1,0}, 13.5f, CAMERA_ORTHOGRAPHIC } ;
            motor::vector<sample> samples ;
            samples.reserve( opts.frames ) ;
            auto const started = clock_t::now() ;
            auto previous = started ;
            sample current {} ;
            size_t tick = 0 ;
            bool_t captured = false ;
            bool_t capture_ok = true ;
            while( !WindowShouldClose() )
            {
                auto const begin = clock_t::now() ;
                current.frame = milliseconds( previous, begin ) ;
                if( tick > opts.warmup ) samples.emplace_back( current ) ;
                if( samples.size() == opts.frames ) break ;
                if( milliseconds( started, begin ) > 300000.0 ) break ;
                previous = begin ;

                float_t const angle = float_t(tick % 360) * 0.01745329252f ;
                for( auto & object : objects ) object.world = world_matrix( object, angle ) ;
                auto const updated = clock_t::now() ;
                current.update = milliseconds( begin, updated ) ;
                {
                    BeginDrawing() ;
                    ClearBackground( BLACK ) ;
                    BeginMode3D( camera ) ;
                    rlDisableBackfaceCulling() ;
                    rlDisableColorBlend() ;
                    for( auto const & object : objects )
                    {
                        SetShaderValue( shader, color_location, object.color, SHADER_UNIFORM_VEC4 ) ;
                        DrawMesh( mesh, material, object.world ) ;
                    }
                    EndMode3D() ;
                    rlEnableColorBlend() ;
                    rlEnableBackfaceCulling() ;
                    if( !captured && !opts.capture.empty() )
                    {
                        rlDrawRenderBatchActive() ;
                        auto image = LoadImageFromScreen() ;
                        capture_ok = ExportImage( image, opts.capture.c_str() ) ;
                        UnloadImage( image ) ;
                        captured = true ;
                    }
                    EndDrawing() ;
                }
                current.submit = milliseconds( updated, clock_t::now() ) ;
                ++tick ;
            }
            bool_t const completed = samples.size() == opts.frames && capture_ok ;
            if( completed ) report( opts, samples ) ;
            result = motor::log::global_t::test( "Raylib benchmark completed", completed ) ? 0 : 1 ;
            UnloadMesh( mesh ) ;
            UnloadMaterial( material ) ;
        }
        CloseWindow() ;
        return result ;
    }
}

int main( int argc, char ** argv )
{
    int result = 0 ;
    {
        this_file::options opts ;
        if( argc == 2 && std::strcmp( argv[1], "--help" ) == 0 ) this_file::usage() ;
        else if( !this_file::parse( argc, argv, opts ) )
        {
            this_file::usage() ;
            result = 2 ;
        }
        else result = this_file::run( opts ) ;
    }
    motor::log::global_t::deinit() ;
    return result ;
}
