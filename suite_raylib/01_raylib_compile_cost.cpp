#include "../comparison/compile_cost.hpp"
#include <motor/concurrent/typedefs.h>
#include <raylib.h>
#include <rlgl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <cmath>

namespace this_file
{
    using namespace motor::core::types ;
    using clock = compile_cost::clock ;
    char const * vertex = R"(#version 330
        in vec3 vertexPosition;
        uniform mat4 mvp;
        void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
    )" ;
    char const * simple_fragment = R"(#version 330
        uniform vec4 u_color;
        out vec4 finalColor;
        void main() { finalColor = u_color; }
    )" ;

    // Do not use LoadShaderFromMemory/BeginShaderMode on the worker. Only the
    // verified rlgl compile/link path executes there, with immutable default IDs.
    // The worker owns a separate shared GL context and publishes a finished program.
    class compiler
    {
        GLFWwindow * _context = nullptr ;
        std::mutex _mutex ;
        std::condition_variable _wake ;
        bool_t _stop = false, _queued = false ;
        motor::string_t _source ;
        motor::concurrent::thread_t _thread ;
        using finish_fn = void (
#ifdef _WIN32
            __stdcall
#endif
            *)(void) ;
        finish_fn _finish = nullptr ;
    public:
        std::atomic_bool done { false } ;
        unsigned int program = 0 ;
        double_t compile_ms = 0.0 ;

        explicit compiler( GLFWwindow * context ) : _context( context )
        {
            _finish = reinterpret_cast<finish_fn>( glfwGetProcAddress( "glFinish" ) ) ;
            if( _context ) _thread = motor::concurrent::thread_t( [this]( void_t )
            {
                glfwMakeContextCurrent( _context ) ;
                for( ;; )
                {
                    motor::string_t code ;
                    {
                        std::unique_lock<std::mutex> lock( _mutex ) ;
                        _wake.wait( lock, [this]( void_t ) { return _stop || _queued ; } ) ;
                        if( _stop ) break ;
                        code = std::move( _source ) ; _queued = false ;
                    }
                    compile( code ) ;
                }
                glfwMakeContextCurrent( nullptr ) ;
            } ) ;
        }
        ~compiler( void_t )
        { stop() ; }
        void_t stop( void_t )
        {
            { std::lock_guard<std::mutex> lock( _mutex ) ; _stop = true ; }
            _wake.notify_one() ; if( _thread.joinable() ) _thread.join() ;
        }
        void_t compile( motor::string_in_t code )
        {
            auto const begin = clock::now() ;
            program = rlLoadShaderProgram( vertex, code.c_str() ) ;
            // Completion before cross-context publication. The synchronous control
            // deliberately uses the same completion step. Not a per-frame glFinish.
            if( _finish ) _finish() ;
            compile_ms = compile_cost::ms( begin, clock::now() ) ;
            done.store( true, std::memory_order_release ) ;
        }
        void_t submit( motor::string_t code )
        {
            done.store( false, std::memory_order_relaxed ) ;
            if( !_context ) { compile( code ) ; return ; }
            { std::lock_guard<std::mutex> lock( _mutex ) ; _source = std::move( code ) ; _queued = true ; }
            _wake.notify_one() ;
        }
    } ;

    Shader adopt( unsigned int const program )
    {
        Shader shader { program, static_cast<int *>( MemAlloc( RL_MAX_SHADER_LOCATIONS * sizeof(int) ) ) } ;
        for( int i = 0 ; i < RL_MAX_SHADER_LOCATIONS ; ++i ) shader.locs[i] = -1 ;
        shader.locs[SHADER_LOC_VERTEX_POSITION] = GetShaderLocationAttrib( shader, "vertexPosition" ) ;
        shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation( shader, "mvp" ) ;
        return shader ;
    }

    void_t draw( Shader const shader, int const color_location, float_t const x, float_t const y )
    {
        float_t const color[] = { 0.25f, 0.9f, 0.4f, 1.0f } ;
        SetShaderValue( shader, color_location, color, SHADER_UNIFORM_VEC4 ) ;
        BeginShaderMode( shader ) ;
        DrawRectangleV( { x, y }, { 77.0f, 77.0f }, WHITE ) ;
        rlDrawRenderBatchActive() ;
        EndShaderMode() ;
    }
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    bool_t worker = false ; uint32_t seed = 1 ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg = argv[i] ;
        if( arg == "--worker" ) worker = true ;
        else if( arg == "--sync" ) worker = false ;
        else if( arg == "--seed" && i + 1 < argc )
        {
            auto const text = argv[++i] ; auto const end = text + std::strlen( text ) ;
            auto const result = std::from_chars( text, end, seed ) ;
            if( result.ec != std::errc{} || result.ptr != end ) return 2 ;
        }
        else
        {
            motor::log::global_t::status( "01_raylib_compile_cost [--sync|--worker] [--seed N]" ) ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
    SetTraceLogLevel( LOG_NONE ) ;
    motor::log::global_t::status( "[compile-cost] raylib seed=" + motor::to_string( seed ) +
        " operations=" + motor::to_string( compile_cost::operations ) + (worker ? " worker" : " sync") ) ;
    InitWindow( 640, 640, worker ? "raylib | compile worker" : "raylib | compile synchronous" ) ;
    if( !IsWindowReady() ) return 1 ;
    glfwSwapInterval( 0 ) ;
    GLFWwindow * shared = nullptr ;
    if( worker )
    {
        glfwDefaultWindowHints() ;
        glfwWindowHint( GLFW_VISIBLE, GLFW_FALSE ) ;
        glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 3 ) ;
        glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 3 ) ;
        glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE ) ;
        shared = glfwCreateWindow( 1, 1, "compile worker", nullptr, glfwGetCurrentContext() ) ;
        if( !shared ) { CloseWindow() ; return 1 ; }
    }
    bool_t ok = true ;
    compile_cost::measurements measured ;
    {
        this_file::compiler compiler( shared ) ;
        Shader background = LoadShaderFromMemory( this_file::vertex, this_file::simple_fragment ) ;
        int const background_color = GetShaderLocation( background, "u_color" ) ;
        ok &= background.id != rlGetShaderIdDefault() && background_color >= 0 ;
        Shader compiled {} ; int compiled_color = -1 ;
        bool_t active = false, draw_compiled = false ;
        size_t idle = 0, revision = 0, progress = 0 ;
        auto const started = this_file::clock::now() ;
        auto request = started ;
        while( ok && !WindowShouldClose() )
        {
            auto const begin = this_file::clock::now() ;
            bool_t frame_active = active ;
            if( compile_cost::ms( started, begin ) > 60000.0 ) { ok = false ; break ; }
            if( !active && idle >= compile_cost::idle_frames )
            {
                if( revision == compile_cost::rounds ) break ;
                ++revision ;
                motor::string_t code = "#version 330\nuniform vec4 u_color; out vec4 finalColor; void main() {\n" +
                    compile_cost::body( seed, revision, false ) + "}\n" ;
                request = this_file::clock::now() ; active = frame_active = true ; progress = 0 ;
                compiler.submit( std::move( code ) ) ;
            }
            if( active && compiler.done.load( std::memory_order_acquire ) )
            {
                ok &= compiler.program != 0 && compiler.program != rlGetShaderIdDefault() ;
                if( !ok ) break ;
                if( compiled.id ) UnloadShader( compiled ) ;
                compiled = this_file::adopt( compiler.program ) ;
                compiled_color = GetShaderLocation( compiled, "u_color" ) ;
                ok &= compiled_color >= 0 ; draw_compiled = true ;
                auto const latency = compile_cost::ms( request, this_file::clock::now() ) ;
                measured.latency.emplace_back( latency ) ; measured.progress_frames += progress ;
                motor::log::global_t::status( "[compile-cost] raylib revision=" + motor::to_string( revision ) +
                    " compile_ms=" + motor::to_string( compiler.compile_ms ) + " ready_ms=" + motor::to_string( latency ) +
                    " frames_before_delivery=" + motor::to_string( progress ) ) ;
                active = false ; idle = 0 ;
            }
            BeginDrawing() ; ClearBackground( { 9, 11, 14, 255 } ) ;
            float_t const x = 281.0f + 224.0f * std::sin( float_t( GetTime() ) ) ;
            for( size_t i = 0 ; i < 13 ; ++i ) this_file::draw( background, background_color, x, 537.0f ) ;
            if( draw_compiled )
            { this_file::draw( compiled, compiled_color, 90.0f, 394.0f ) ; draw_compiled = false ; }
            EndDrawing() ;
            auto const end = this_file::clock::now() ;
            if( frame_active ) measured.active.emplace_back( compile_cost::ms( begin, end ) ) ;
            else if( idle >= 30 ) measured.idle.emplace_back( compile_cost::ms( begin, end ) ) ;
            // A completed frame before observing readiness, not a GPU timer or
            // proof that the compiler was busy for this entire frame.
            if( active ) ++progress ; else ++idle ;
        }
        ok &= measured.latency.size() == compile_cost::rounds ;
        compiler.stop() ;
        if( compiler.program && compiler.program != compiled.id && compiler.program != rlGetShaderIdDefault() )
            rlUnloadShaderProgram( compiler.program ) ;
        if( compiled.id ) UnloadShader( compiled ) ;
        UnloadShader( background ) ;
    }
    if( shared ) glfwDestroyWindow( shared ) ;
    CloseWindow() ;
    measured.report( worker ? "raylib-worker" : "raylib-sync" ) ;
    motor::log::global_t::test( "[compile-cost] all programs delivered and used", ok ) ;
    motor::log::global_t::deinit() ;
    return ok ? 0 : 1 ;
}
