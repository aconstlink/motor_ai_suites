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
#include <motor/tool/imgui/imgui.h>

#include <cstring>
#include <iostream>
#include <chrono>

namespace sample
{
    class window_lifecycle_app : public motor::application::app
    {
        motor::graphics::geometry_object_t _geometry;
        motor::graphics::state_object_t _state;
        motor::graphics::msl_object_mtr_t _shader = nullptr;
        static constexpr window_id_t invalid_window = window_id_t( -1 );
        window_id_t _main_window = invalid_window;
        window_id_t _secondary_window = invalid_window;
        bool _want_secondary = false;
        bool _closing_secondary = false;
        bool _secondary_ready = false;
        bool _quitting = false;
        float _color[3] = { 0.9f, 0.4f, 0.15f };
        motor::graphics::data_variable<motor::math::vec4f_t>* _color_variable = nullptr;
        bool _smoke;
        unsigned _step = 0;
        unsigned _step_frames = 0;
        unsigned _open_count = 0;
        unsigned _close_count = 0;
        unsigned _ready_count = 0;
        bool* _smoke_passed;
        std::chrono::steady_clock::time_point _started;

        struct vertex { motor::math::vec3f_t position; };

        window_id_t open_window( motor::application::graphics_generation gen,
            char const* title, int x ) noexcept
        {
            motor::application::window_info_t info;
            info.window_name = title;
            info.x = x;
            info.y = 100;
            info.w = 640;
            info.h = 480;
            info.gen = gen;
            auto const id = create_window( info );
            send_window_message( id, []( auto& window )
            {
                window.send_message( motor::application::show_message( { true } ) );
                window.send_message( motor::application::vsync_message_t( { true } ) );
            } );
            return id;
        }

    public:
        window_lifecycle_app( bool smoke, bool* passed ) noexcept
            : _smoke( smoke ), _smoke_passed( passed ) {}

        void on_init() noexcept override
        {
            _started = std::chrono::steady_clock::now();
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
                            vec4_t u_color ;
                            void main()
                            {
                                out.color = u_color ;
                            }
                        }
                    }
                )" );
                shader.link_geometry( "triangle" );
                // shader variables
                {
                    auto vars = motor::shared( motor::graphics::variable_set_t() );
                    _color_variable = vars->data_variable<motor::math::vec4f_t>( "u_color" );
                    _color_variable->set( motor::math::vec4f_t( _color[0], _color[1], _color[2], 1.0f ) );
                    shader.add_variable_set( motor::move( vars ) );
                }

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

            _main_window = open_window( motor::application::graphics_generation::gen4_gl4,
                "motor | lifecycle | OpenGL 4", 60 );
        }

        void on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            // UI edits are staged until the shared frame preparation boundary.
            _color_variable->set( motor::math::vec4f_t( _color[0], _color[1], _color[2], 1.0f ) );
            if( _want_secondary && _secondary_window == invalid_window && !_quitting )
            {
                _secondary_window = open_window(
                    motor::application::graphics_generation::gen4_d3d11,
                    "motor | lifecycle | D3D11", 740 );
                _secondary_ready = false;
                ++_open_count;
                motor::log::global::status( "[lifecycle] D3D11 window requested" );
            }
            else if( !_want_secondary && _secondary_window != invalid_window && !_closing_secondary )
            {
                _closing_secondary = true;
                send_window_message( _secondary_window, []( auto& window )
                {
                    window.send_message( motor::application::close_message( { true } ) );
                } );
            }
        }

        void on_render( window_id_t const id,
            motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            // Each frontend configures the same objects for its own backend.
            if( data.first_frame )
            {
                fe->configure<motor::graphics::state_object_t>( &_state );
                fe->configure<motor::graphics::geometry_object_t>( &_geometry );
                fe->configure<motor::graphics::msl_object_t>( _shader );
            }
            if( data.last_frame )
            {
                fe->release<motor::graphics::msl_object_t>( _shader );
                fe->release<motor::graphics::geometry_object_t>( &_geometry );
                fe->release<motor::graphics::state_object_t>( &_state );
                if( id == _secondary_window )
                {
                    _secondary_window = invalid_window;
                    _closing_secondary = false;
                    _secondary_ready = false;
                    ++_close_count;
                    motor::log::global::status( "[lifecycle] D3D11 last frame; OpenGL remains active" );
                }
                return;
            }
            fe->push( &_state );
            // Readiness is backend-specific; compilation is asynchronous.
            auto const status = fe->decode( _shader );
            if( status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok )
            {
                if( id == _secondary_window && !_secondary_ready )
                {
                    _secondary_ready = true;
                    ++_ready_count;
                    motor::log::global::status( "[lifecycle] D3D11 shader ready" );
                }
                motor::graphics::gen4::backend_t::render_detail_t detail;
                detail.varset = 0;
                fe->render( _shader, detail );
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }

        bool on_tool( window_id_t const id,
            motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _main_window || _quitting ) return false;
            ImGui::SetNextWindowPos( ImVec2( 12.0f, 48.0f ), ImGuiCond_FirstUseEver );
            ImGui::SetNextWindowSize( ImVec2( 300.0f, 110.0f ), ImGuiCond_FirstUseEver );
            if( ImGui::Begin( "Window lifecycle", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::BeginDisabled( _smoke || _closing_secondary );
                ImGui::Checkbox( "D3D11 window", &_want_secondary );
                ImGui::ColorEdit3( "Color", _color );
                ImGui::EndDisabled();
                ImGui::Text( "Opened: %u   Closed: %u", _open_count, _close_count );
            }
            ImGui::End();
            return true;
        }

        void on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _smoke && std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                motor::log::global::error( "[lifecycle] smoke test timed out" );
                _quitting = true;
                close();
            }
        }

        void on_frame_done() noexcept override
        {
            if( !_smoke || _quitting ) return;
            ++_step_frames;
            if( _step_frames < 90 ) return;
            bool advance = false;
            switch( _step )
            {
            case 0: _want_secondary = true; advance = true; break;
            case 1:
                if( _secondary_ready )
                {
                    _color[0] = 0.15f; _color[1] = 0.85f; _color[2] = 0.35f;
                    advance = true;
                }
                break;
            case 2: _want_secondary = false; advance = true; break;
            case 3:
                if( _secondary_window == invalid_window )
                {
                    _want_secondary = true;
                    advance = true;
                }
                break;
            case 4:
                if( _secondary_ready )
                {
                    _color[0] = 0.2f; _color[1] = 0.45f; _color[2] = 0.95f;
                    advance = true;
                }
                break;
            case 5: _want_secondary = false; advance = true; break;
            case 6:
                if( _secondary_window == invalid_window )
                {
                    *_smoke_passed = _open_count == 2 && _close_count == 2 && _ready_count == 2;
                    motor::log::global::status( *_smoke_passed ?
                        "[lifecycle] smoke test passed" : "[lifecycle] smoke test failed" );
                    _quitting = true;
                    close();
                }
                break;
            }
            if( advance ) { ++_step; _step_frames = 0; }
        }

        void on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( !state.close_changed ) return;
            if( id == _main_window )
            {
                _quitting = true;
                close();
            }
            else if( id == _secondary_window )
            {
                _want_secondary = false;
                _closing_secondary = true;
            }
        }

        void on_shutdown() noexcept override
        {
            _color_variable = nullptr;
            motor::release( motor::move( _shader ) );
        }
    };
}

int main( int argc, char** argv )
{
    bool smoke = false;
    for( int i = 1; i < argc; ++i )
    {
        if( std::strcmp( argv[i], "--smoke" ) == 0 ) smoke = true;
        else
        {
            std::cout << "Usage: 01_window_lifecycle [--smoke]\n";
            return std::strcmp( argv[i], "--help" ) == 0 ? 0 : 2;
        }
    }
#ifndef _WIN32
    std::cerr << "This D3D11 lifecycle sample requires Windows.\n";
    return 2;
#endif
    bool smoke_passed = false;
    motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
        motor::shared( sample::window_lifecycle_app( smoke, &smoke_passed ) ) );
    auto const result = carrier->exec();
    motor::memory::release_ptr( carrier );
    motor::concurrent::global::deinit();
    motor::log::global::deinit();
    motor::profiling::global::deinit();
    motor::io::global::deinit();
    motor::memory::global::dump_to_std();
    return result != 0 ? result : ( smoke && !smoke_passed ? 1 : 0 );
}
