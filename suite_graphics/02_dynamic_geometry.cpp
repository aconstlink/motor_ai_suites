#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>

namespace sample
{
    class dynamic_geometry_app : public motor::application::app
    {
        static constexpr size_t invalid = size_t( -1 );
        struct vertex { motor::math::vec3f_t position; };
        struct object_parameters
        {
            float color[3];
            float position[2];
            size_t set_id = invalid;
            motor::graphics::data_variable<motor::math::vec4f_t>* color_var = nullptr;
            motor::graphics::data_variable<motor::math::vec4f_t>* position_var = nullptr;

            void write() const noexcept
            {
                if( color_var == nullptr ) return;
                color_var->set( motor::math::vec4f_t( color[0], color[1], color[2], 1.0f ) );
                position_var->set( motor::math::vec4f_t( position[0], position[1], 0.0f, 0.0f ) );
            }
        };
        struct window_state
        {
            window_id_t id = invalid;
            bool ready = false;
            bool square_configured = false;
            size_t revision = 0;
        };

        motor::graphics::geometry_object_t _triangle;
        motor::graphics::geometry_object_t _square;
        motor::graphics::state_object_t _state;
        motor::graphics::msl_object_mtr_t _shader = nullptr;
        object_parameters _triangle_data { { 0.95f, 0.35f, 0.15f }, { -0.45f, -0.25f } };
        object_parameters _square_data { { 0.15f, 0.7f, 0.95f }, { 0.45f, -0.25f } };
        std::array<window_state, 2> _windows;
        size_t _window_count;
        size_t _triangle_link = invalid;
        size_t _square_link = invalid;
        size_t _changed_link = invalid;
        size_t _revision = 0;
        unsigned _added = 0;
        unsigned _removed = 0;
        bool _want_square = false;
        bool _smoke;
        bool* _passed;
        unsigned _step = 0;
        unsigned _frames = 0;
        std::chrono::steady_clock::time_point _started;

        bool all_ready() const noexcept
        {
            for( size_t i = 0; i < _window_count; ++i ) if( !_windows[i].ready ) return false;
            return true;
        }

        void make_set( object_parameters& data ) noexcept
        {
            auto vars = motor::shared( motor::graphics::variable_set_t() );
            data.color_var = vars->data_variable<motor::math::vec4f_t>( "u_color" );
            data.position_var = vars->data_variable<motor::math::vec4f_t>( "u_offset" );
            data.write();
            data.set_id = _shader->add_variable_set( motor::move( vars ) );
        }

        static motor::graphics::geometry_object_t make_geometry( bool square )
        {
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element( motor::graphics::vertex_attribute::position,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .resize( square ? 4 : 3 ).update<vertex>( [square]( vertex* v, size_t )
                {
                    v[0].position = motor::math::vec3f_t( -0.28f, -0.35f, 0.5f );
                    v[1].position = motor::math::vec3f_t( square ? -0.28f : 0.0f, 0.35f, 0.5f );
                    v[2].position = motor::math::vec3f_t( 0.28f, square ? 0.35f : -0.35f, 0.5f );
                    if( square ) v[3].position = motor::math::vec3f_t( 0.28f, -0.35f, 0.5f );
                } );
            auto ib = motor::graphics::index_buffer_t()
                .set_layout_element( motor::graphics::type::tuint )
                .resize( square ? 6 : 3 ).update<unsigned int>( [square]( unsigned int* v, size_t )
                {
                    v[0] = 0; v[1] = 1; v[2] = 2;
                    if( square ) { v[3] = 0; v[4] = 2; v[5] = 3; }
                } );
            return motor::graphics::geometry_object_t( square ? "dynamic_square" : "fixed_triangle",
                motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) );
        }

    public:
        dynamic_geometry_app( bool dual, bool smoke, bool* passed ) noexcept
            : _window_count( dual ? 2 : 1 ), _smoke( smoke ), _passed( passed ) {}

        void on_init() noexcept override
        {
            _started = std::chrono::steady_clock::now();
            // geometry
            {
                _triangle = make_geometry( false );
                _square = make_geometry( true );
            }

            // msl object
            {
                motor::graphics::msl_object_t shader( "shared_geometry_shader" );
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config shared_geometry_shader
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ;
                            out vec4_t pos : position ;
                            vec4_t u_offset ;
                            void main()
                            {
                                out.pos = vec4_t( in.pos + u_offset.xyz, 1.0 ) ;
                            }
                        }
                        pixel_shader
                        {
                            out vec4_t color : color ;
                            vec4_t u_color ;
                            void main() { out.color = u_color ; }
                        }
                    }
                )" );
                _shader = motor::shared( std::move( shader ) );
                _triangle_link = _shader->link_geometry( "fixed_triangle" );
                make_set( _triangle_data );
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
                _state = motor::graphics::state_object_t( "dynamic geometry state" );
                _state.add_render_state_set( states );
            }

            for( size_t i = 0; i < _window_count; ++i )
            {
                motor::application::window_info_t info;
                info.x = int( 60 + i * 680 ); info.y = 100;
                info.w = 640; info.h = 480;
                info.window_name = "motor | dynamic geometry";
                info.gen = i == 0 ? motor::application::graphics_generation::gen4_gl4 :
                    motor::application::graphics_generation::gen4_d3d11;
                _windows[i].id = create_window( info );
                send_window_message( _windows[i].id, []( auto& w )
                {
                    w.send_message( motor::application::show_message( { true } ) );
                    w.send_message( motor::application::vsync_message_t( { true } ) );
                } );
            }
        }

        void on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            // All windows finished the previous frame before this callback.
            if( all_ready() && _want_square != ( _square_link != invalid ) )
            {
                if( _want_square )
                {
                    _square_link = _shader->link_geometry( "dynamic_square" );
                    make_set( _square_data );
                    _changed_link = _square_link;
                    ++_added;
                    motor::log::global::status( "[dynamic] square linked with new variable set" );
                }
                else
                {
                    _changed_link = _square_link;
                    _shader->unlink_geometry( _square_link );
                    _shader->drop_variable_set( _square_data.set_id );
                    _square_link = invalid;
                    _square_data.set_id = invalid;
                    _square_data.color_var = nullptr;
                    _square_data.position_var = nullptr;
                    ++_removed;
                    motor::log::global::status( "[dynamic] square unlinked; variable set dropped" );
                }
                ++_revision;
            }
            _triangle_data.write();
            _square_data.write();
        }

        void on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            auto& window = _windows[id == _windows[0].id ? 0 : 1];
            if( data.first_frame )
            {
                fe->configure<motor::graphics::state_object_t>( &_state );
                fe->configure<motor::graphics::geometry_object_t>( &_triangle );
                fe->configure<motor::graphics::msl_object_t>( _shader );
            }
            if( data.last_frame )
            {
                fe->release<motor::graphics::msl_object_t>( _shader );
                if( window.square_configured ) fe->release<motor::graphics::geometry_object_t>( &_square );
                fe->release<motor::graphics::geometry_object_t>( &_triangle );
                fe->release<motor::graphics::state_object_t>( &_state );
                return;
            }
            auto const status = fe->decode( _shader );
            window.ready = status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok;
            fe->push( &_state );
            if( window.ready )
            {
                if( window.revision != _revision )
                {
                    if( _square_link != invalid && !window.square_configured )
                    {
                        fe->configure<motor::graphics::geometry_object_t>( &_square );
                        window.square_configured = true;
                    }
                    // Notify every backend, including removal when there is no square draw.
                    fe->update_geometry_link( _shader, _changed_link );
                    window.revision = _revision;
                }
                // triangle
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail;
                    detail.geo = _triangle_link;
                    detail.varset = _triangle_data.set_id;
                    fe->render( _shader, detail );
                }

                if( _square_link != invalid )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail;
                    detail.geo = _square_link;
                    detail.varset = _square_data.set_id;
                    fe->render( _shader, detail );
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }

        bool on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id ) return false;
            ImGui::SetNextWindowPos( ImVec2( 12, 48 ), ImGuiCond_FirstUseEver );
            ImGui::SetNextWindowSize( ImVec2( 360, 170 ), ImGuiCond_FirstUseEver );
            if( ImGui::Begin( "Shared shader", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::BeginDisabled( _smoke || !all_ready() );
                ImGui::PushItemWidth( 180.0f );
                ImGui::Checkbox( "Square", &_want_square );
                ImGui::ColorEdit3( "Triangle color", _triangle_data.color );
                ImGui::SliderFloat2( "Triangle position", _triangle_data.position, -0.7f, 0.7f );
                ImGui::Separator();
                ImGui::ColorEdit3( "Square color", _square_data.color );
                ImGui::SliderFloat2( "Square position", _square_data.position, -0.7f, 0.7f );
                ImGui::PopItemWidth();
                ImGui::EndDisabled();
                ImGui::Text( "Added: %u   Removed: %u", _added, _removed );
            }
            ImGui::End();
            return true;
        }

        void on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _smoke && std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                motor::log::global::error( "[dynamic] smoke timed out" );
                close();
            }
        }

        void on_frame_done() noexcept override
        {
            if( !_smoke || !all_ready() || ++_frames < 100 ) return;
            _frames = 0;
            switch( _step++ )
            {
            case 0: _want_square = true; break;
            case 1: _square_data.position[1] = -0.45f; break;
            case 2: _want_square = false; break;
            case 3:
                _square_data.color[0] = 0.85f; _square_data.color[1] = 0.2f; _square_data.color[2] = 0.8f;
                _square_data.position[1] = -0.1f;
                _want_square = true;
                break;
            case 4: _want_square = false; break;
            case 5:
                *_passed = _added == 2 && _removed == 2 && _square_link == invalid;
                motor::log::global::status( *_passed ? "[dynamic] lifecycle smoke passed (visual check required)" :
                    "[dynamic] lifecycle smoke failed" );
                close();
                break;
            }
        }

        void on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        {
            if( s.close_changed ) close();
        }

        void on_shutdown() noexcept override
        {
            motor::release( motor::move( _shader ) );
        }
    };
}

int main( int argc, char** argv )
{
    bool dual = false, smoke = false, passed = false;
    for( int i = 1; i < argc; ++i )
    {
        if( std::strcmp( argv[i], "--dual" ) == 0 ) dual = true;
        else if( std::strcmp( argv[i], "--smoke" ) == 0 ) smoke = true;
        else
        {
            std::cout << "Usage: 02_dynamic_geometry [--dual] [--smoke]\n";
            return std::strcmp( argv[i], "--help" ) == 0 ? 0 : 2;
        }
    }
#ifndef _WIN32
    if( dual ) { std::cerr << "D3D11 requires Windows.\n"; return 2; }
#endif
    motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
        motor::shared( sample::dynamic_geometry_app( dual, smoke, &passed ) ) );
    auto const result = carrier->exec();
    motor::memory::release_ptr( carrier );
    motor::concurrent::global::deinit();
    motor::log::global::deinit();
    motor::profiling::global::deinit();
    motor::io::global::deinit();
    motor::memory::global::dump_to_std();
    return result != 0 ? result : ( smoke && !passed ? 1 : 0 );
}
