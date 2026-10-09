#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>

#include <cstdlib>
#include <cstring>

namespace sample
{
    using motor::core::types::void_t;

    class triangle_app : public motor::application::app
    {
        motor::graphics::geometry_object_t _geometry;
        motor::graphics::state_object_t _state;
        motor::graphics::msl_object_mtr_t _shader = nullptr;
        bool _dual;
        size_t _frames_left;
        bool _limited;

        struct vertex { motor::math::vec3f_t position; };

        void open_window( motor::application::graphics_generation gen,
            char const* title, int x ) noexcept
        {
            motor::application::window_info_t info;
            info.window_name = title;
            info.x = x;
            info.y = 100;
            info.w = 640;
            info.h = 480;
            info.gen = gen;
            send_window_message( create_window( info ), []( auto& window )
            {
                window.send_message( motor::application::show_message( { true } ) );
                window.send_message( motor::application::vsync_message_t( { true } ) );
            } );
        }

    public:
        triangle_app( bool dual, size_t frames ) noexcept
            : _dual( dual ), _frames_left( frames ), _limited( frames != 0 ) {}

        void on_init() noexcept override
        {
            // geometry
            {
                auto vertices = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .resize( 3 ).update<vertex>( []( vertex* data, size_t )
                    {
                        data[0].position = motor::math::vec3f_t( -0.75f, -0.65f, 0.5f );
                        data[1].position = motor::math::vec3f_t( 0.0f, 0.75f, 0.5f );
                        data[2].position = motor::math::vec3f_t( 0.75f, -0.65f, 0.5f );
                    } );
                auto indices = motor::graphics::index_buffer_t()
                    .set_layout_element( motor::graphics::type::tuint )
                    .resize( 3 ).update<unsigned int>( []( unsigned int* data, size_t )
                    {
                        data[0] = 0;
                        data[1] = 1;
                        data[2] = 2;
                    } );
                _geometry = motor::graphics::geometry_object_t( "triangle",
                    motor::graphics::primitive_type::triangles,
                    std::move( vertices ), std::move( indices ) );
            }

            // msl object
            {
                motor::graphics::msl_object_t shader( "triangle_shader" );
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config triangle_shader
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ;
                            out vec4_t pos : position ;
                            out vec2_t uv : texcoord0 ;
                            void main()
                            {
                                out.pos = vec4_t( in.pos, 1.0 ) ;
                                out.uv = in.pos.xy * 0.5 + vec2_t( 0.5, 0.5 ) ;
                            }
                        }
                        pixel_shader
                        {
                            in vec2_t uv : texcoord0 ;
                            out vec4_t color : color ;
                            void main()
                            {
                                out.color = vec4_t( in.uv.x, in.uv.y, 0.7, 1.0 ) ;
                            }
                        }
                    }
                )" );
                shader.link_geometry( "triangle" );
                shader.add_variable_set( motor::shared( motor::graphics::variable_set_t() ) );
                _shader = motor::shared( std::move( shader ), "triangle shader" );
            }

            // render states
            {
                motor::graphics::render_state_sets_t states;
                states.depth_s.do_change = true;
                states.depth_s.ss.do_activate = false;
                states.depth_s.ss.do_depth_write = false;
                states.polygon_s.do_change = true;
                states.polygon_s.ss.do_activate = false;
                states.clear_s.do_change = true;
                states.clear_s.ss.do_activate = true;
                states.clear_s.ss.do_color_clear = true;
                states.clear_s.ss.do_depth_clear = true;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.08f, 0.08f, 0.08f, 1.0f );
                _state = motor::graphics::state_object_t( "triangle state" );
                _state.add_render_state_set( states );
            }

            open_window( motor::application::graphics_generation::gen4_gl4,
                "motor | triangle | OpenGL 4", 60 );
            if( _dual ) open_window( motor::application::graphics_generation::gen4_d3d11,
                "motor | triangle | D3D11", 740 );
        }

        void_t on_first_frame( window_id_t const,
            motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            // Each frontend configures the same objects for its own backend.
            fe->configure<motor::graphics::state_object_t>( &_state );
            fe->configure<motor::graphics::geometry_object_t>( &_geometry );
            fe->configure<motor::graphics::msl_object_t>( _shader );
        }

        void_t on_last_frame( window_id_t const,
            motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>( _shader );
            fe->release<motor::graphics::geometry_object_t>( &_geometry );
            fe->release<motor::graphics::state_object_t>( &_state );
        }

        void on_render( window_id_t const,
            motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->push( &_state );
            // draw
            {
                motor::graphics::gen4::backend_t::render_detail_t detail;
                detail.varset = 0;
                fe->render( _shader, detail );
            }

            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }

        void on_frame_done() noexcept override
        {
            if( _limited && _frames_left != 0 && --_frames_left == 0 ) close();
        }

        void on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.close_changed ) close();
        }

        void on_shutdown() noexcept override
        {
            motor::release( motor::move( _shader ) );
        }
    };
}

int main( int argc, char** argv )
{
    bool dual = false;
    size_t frames = 0;
    for( int i = 1; i < argc; ++i )
    {
        if( std::strcmp( argv[i], "--dual" ) == 0 ) dual = true;
        else if( std::strcmp( argv[i], "--frames" ) == 0 && i + 1 < argc )
        {
            char* end = nullptr;
            char const* value = argv[++i];
            auto const count = std::strtoul( value, &end, 10 );
            if( value[0] == '-' || end == value || *end != '\0' || count == 0 ) return 2;
            frames = count;
        }
        else
        {
            motor::log::global_t::status( "Usage: 00_triangle [--dual] [--frames positive-count]" );
            motor::log::global_t::deinit();
            return std::strcmp( argv[i], "--help" ) == 0 ? 0 : 2;
        }
    }
#ifndef _WIN32
    if( dual )
    {
        motor::log::global_t::error( "The D3D11 comparison requires Windows." );
        motor::log::global_t::deinit();
        return 2;
    }
#endif
    motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
        motor::shared( sample::triangle_app( dual, frames ) ) );
    auto const result = carrier->exec();
    motor::memory::release_ptr( carrier );
    motor::concurrent::global::deinit();
    motor::log::global::deinit();
    motor::profiling::global::deinit();
    motor::io::global::deinit();
    motor::memory::global::dump_to_std();
    return result;
}
