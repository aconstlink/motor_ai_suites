#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/array_object.h>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>

#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;

    struct options
    {
        bool_t dual = true ;
        bool_t d3d_only = false ;
        bool_t smoke = false ;
        bool_t still = false ;
    } ;

    class vertex_pulling_app : public motor::application::app
    {
        struct vertex
        {
            motor::math::vec4f_t position ;
            motor::math::vec4f_t color ;
        } ;

        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            size_t revision = size_t( -1 ) ;
            size_t draws = 0 ;
            size_t updates = 0 ;
        } ;

        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _geometry[2] ;
        motor::graphics::msl_object_mtr_t _shaders[2] = { nullptr, nullptr } ;
        motor::graphics::array_object_t _vertices ;
        motor::graphics::state_object_t _state ;
        size_t _revision = 0 ;
        size_t _frames = 0 ;
        float_t _time = 0.0f ;
        bool_t _quitting = false ;
        std::chrono::steady_clock::time_point _started ;

        static vertex make_vertex( size_t const i, float_t const time ) noexcept
        {
            float_t const x[4] = { -0.30f, 0.30f, 0.30f, -0.30f } ;
            float_t const y[4] = { -0.45f, -0.45f, 0.45f, 0.45f } ;
            float_t const shift = 0.08f * std::sin( time + float_t( i ) ) ;
            return { motor::math::vec4f_t( x[i], y[i] + shift, 0.5f, 1.0f ),
                motor::math::vec4f_t( i < 2 ? 0.95f : 0.15f,
                    i == 1 || i == 2 ? 0.85f : 0.15f,
                    0.45f + 0.35f * std::sin( time ), 1.0f ) } ;
        }

        void_t update_vertices( void_t ) noexcept
        {
            _vertices.data_buffer().update<vertex>( [&]( vertex * ptr, size_t const n )
            {
                for( size_t i = 0 ; i < n ; ++i ) ptr[i] = make_vertex( i, _time ) ;
            } ) ;
            _geometry[1].vertex_buffer().update<vertex>( [&]( vertex * ptr, size_t const n )
            {
                for( size_t i = 0 ; i < n ; ++i ) ptr[i] = make_vertex( i, _time ) ;
            } ) ;
            ++_revision ;
        }

        void_t open_window( size_t const index, bool_t const d3d ) noexcept
        {
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | vertex pulling (left) / VB (right) | D3D11" :
                "motor | vertex pulling (left) / VB (right) | OpenGL 4" ;
            info.x = 40 + int_t( index ) * 680 ;
            info.y = 80 ;
            info.w = 640 ;
            info.h = 480 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[index].id = create_window( info ) ;
            send_window_message( _windows[index].id, []( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { true } ) ) ;
            } ) ;
        }

    public:
        vertex_pulling_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ) {}

        // Existing buffer resize helpers calculate a floating-point ratio with an initial count of zero.
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            // The pulling geometry only supplies the indexed draw. Its VB is deliberately zero.
            for( size_t g = 0 ; g < 2 ; ++g )
            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .resize( 4 ).update<vertex>( []( vertex * ptr, size_t const n )
                    {
                        for( size_t i = 0 ; i < n ; ++i ) ptr[i] = vertex{} ;
                    } ) ;
                auto ib = motor::graphics::index_buffer_t()
                    .set_layout_element( motor::graphics::type::tuint )
                    .resize( 6 ).update<uint_t>( []( uint_t * ptr, size_t )
                    {
                        uint_t const indices[6] = { 2, 0, 1, 0, 2, 3 } ;
                        for( size_t i = 0 ; i < 6 ; ++i ) ptr[i] = indices[i] ;
                    } ) ;
                _geometry[g] = motor::graphics::geometry_object_t(
                    g == 0 ? "pull_geometry" : "reference_geometry",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                auto buffer = motor::graphics::data_buffer_t()
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                buffer.resize( 4 ) ;
                _vertices = motor::graphics::array_object_t( "pulled_vertices", std::move( buffer ) ) ;
                update_vertices() ;
            }
            {
                motor::graphics::msl_object_t shader( "pull_shader" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config pull_shader
                    {
                        vertex_shader
                        {
                            uint_t vid : vertex_id ;
                            data_buffer_t u_vertices ;
                            vec4_t u_offset ;
                            out vec4_t pos : position ;
                            out vec4_t color : color ;
                            void main()
                            {
                                int_t idx = vid * 2 ;
                                out.pos = fetch_data( u_vertices, idx ) + u_offset ;
                                out.color = fetch_data( u_vertices, idx + 1 ) ;
                            }
                        }
                        pixel_shader
                        {
                            in vec4_t color : color ;
                            out vec4_t color : color ;
                            void main() { out.color = in.color ; }
                        }
                    }
                )" ) ;
                shader.link_geometry( "pull_geometry" ) ;
                motor::graphics::variable_set_t vars ;
                vars.array_variable( "u_vertices" )->set( "pulled_vertices" ) ;
                vars.data_variable<motor::math::vec4f_t>( "u_offset" )->set(
                    motor::math::vec4f_t( -0.48f, 0.0f, 0.0f, 0.0f ) ) ;
                shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                _shaders[0] = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "reference_shader" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config reference_shader
                    {
                        vertex_shader
                        {
                            in vec4_t pos : position ;
                            in vec4_t color : color ;
                            vec4_t u_offset ;
                            out vec4_t pos : position ;
                            out vec4_t color : color ;
                            void main()
                            {
                                out.pos = in.pos + u_offset ;
                                out.color = in.color ;
                            }
                        }
                        pixel_shader
                        {
                            in vec4_t color : color ;
                            out vec4_t color : color ;
                            void main() { out.color = in.color ; }
                        }
                    }
                )" ) ;
                shader.link_geometry( "reference_geometry" ) ;
                motor::graphics::variable_set_t vars ;
                vars.data_variable<motor::math::vec4f_t>( "u_offset" )->set(
                    motor::math::vec4f_t( 0.48f, 0.0f, 0.0f, 0.0f ) ) ;
                shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                _shaders[1] = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = false ;
                states.depth_s.ss.do_depth_write = false ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.06f, 0.06f, 0.06f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "pull_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            open_window( 0, _options.d3d_only ) ;
            if( _options.dual ) open_window( 1, true ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting || _options.still ) return ;
            _time += data.sec_dt ;
            update_vertices() ;
        }

        void_t on_first_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            for( auto & geometry : _geometry ) fe->configure<motor::graphics::geometry_object_t>( &geometry ) ;
            fe->configure<motor::graphics::array_object_t>( &_vertices ) ;
            for( auto shader : _shaders ) fe->configure<motor::graphics::msl_object_t>( shader ) ;
        }

        void_t on_last_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto shader : _shaders ) fe->release<motor::graphics::msl_object_t>( shader ) ;
            for( auto & geometry : _geometry ) fe->release<motor::graphics::geometry_object_t>( &geometry ) ;
            fe->release<motor::graphics::array_object_t>( &_vertices ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
            fe->push( &_state ) ;
            bool_t ready = true ;
            for( auto shader : _shaders )
            {
                auto const status = fe->decode( shader ) ;
                ready = ready && status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok ;
            }
            if( ready && !_quitting )
            {
                if( window.revision != _revision )
                {
                    fe->update( &_vertices ) ;
                    fe->update( &_geometry[1] ) ;
                    window.revision = _revision ;
                    ++window.updates ;
                }
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.varset = 0 ;
                for( auto shader : _shaders ) fe->render( shader, detail ) ;
                ++window.draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < ( _options.dual ? 2u : 1u ) ; ++i )
            {
                if( _windows[i].draws < 180 || _windows[i].updates < ( _options.still ? 1u : 60u ) ) return ;
            }
            // Wait additional frames so the final recorded draws have been consumed.
            if( ++_frames < 3 ) return ;
            *_passed = true ;
            _quitting = true ;
            motor::log::global_t::status( "[vertex pulling] smoke passed: shaders ready, draws and updates on every backend; pixels need visual verification" ) ;
            close() ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                _quitting = true ;
                motor::log::global_t::error( "[vertex pulling] smoke timed out" ) ;
                close() ;
            }
        }

        void_t on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & shader : _shaders ) motor::release( motor::move( shader ) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
#ifndef _WIN32
    opts.dual = false ;
#endif
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d_only = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d_only = true ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else
        {
            motor::log::global_t::status( "Usage: 06_vertex_pulling [--gl-only|--d3d-only] [--smoke] [--still]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.d3d_only )
    {
        motor::log::global_t::error( "D3D11 requires Windows." ) ;
        motor::log::global_t::deinit() ;
        return 2 ;
    }
#endif
    bool passed = false ;
    int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::vertex_pulling_app( opts, &passed ) ) ) ;
        result = carrier->exec() ;
        motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global::deinit() ;
    motor::log::global::deinit() ;
    motor::profiling::global::deinit() ;
    motor::io::global::deinit() ;
    auto const remaining = motor::memory::global::dump_to_std() ;
    return result != 0 ? result : ( remaining != 0 || ( opts.smoke && !passed ) ? 1 : 0 ) ;
}
