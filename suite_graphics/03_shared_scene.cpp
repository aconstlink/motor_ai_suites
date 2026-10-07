#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/math/matrix/matrix4.hpp>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/string>

#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;

    struct options
    {
        bool_t dual = true ;
        bool_t d3d_only = false ;
        bool_t eager = false ;
        bool_t smoke = false ;
        bool_t still = false ;
    } ;

    class shared_scene_app : public motor::application::app
    {
        static constexpr size_t invalid = size_t( -1 ) ;
        struct vertex
        {
            motor::math::vec3f_t position ;
            motor::math::vec3f_t shade ;
        } ;
        struct object_data
        {
            size_t set_id = invalid ;
            motor::graphics::data_variable<motor::math::mat4f_t> * world = nullptr ;
        } ;
        struct window_data
        {
            window_id_t id = invalid ;
            bool_t created = false ;
            bool_t shader_ready = false ;
            size_t draws = 0 ;
        } ;

        options _options ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _cube ;
        motor::graphics::state_object_t _state ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        motor::gfx::generic_camera_t _camera ;
        size_t _geometry_id = invalid ;
        object_data _objects[3] ;
        window_data _windows[2] ;
        bool_t _sets_created = false ;
        bool_t _want_secondary = true ;
        bool_t _closing_secondary = false ;
        bool_t _quitting = false ;
        bool_t _paused = false ;
        float_t _time = 0.0f ;
        size_t _phase = 0 ;
        size_t _phase_frames = 0 ;
        size_t _opens = 0 ;
        size_t _closes = 0 ;
        size_t _ready_count = 0 ;
        std::chrono::steady_clock::time_point _started ;

        static motor::math::mat4f_t identity( void_t ) noexcept
        {
            motor::math::mat4f_t m ;
            m[0] = m[5] = m[10] = m[15] = 1.0f ;
            return m ;
        }

        static motor::math::mat4f_t world_matrix(
            float_t const x, float_t const y, float_t const angle ) noexcept
        {
            auto m = identity() ;
            float_t const c = std::cos( angle ), s = std::sin( angle ) ;
            float_t const cx = std::cos( 0.35f ), sx = std::sin( 0.35f ) ;
            m.set_column( 0, motor::math::vec4f_t( c, 0.0f, -s, 0.0f ) ) ;
            m.set_column( 1, motor::math::vec4f_t( s*sx, cx, c*sx, 0.0f ) ) ;
            m.set_column( 2, motor::math::vec4f_t( s*cx, -sx, c*cx, 0.0f ) ) ;
            m.set_column( 3, motor::math::vec4f_t( x, y, 0.0f, 1.0f ) ) ;
            return m ;
        }

        void_t fail( char_cptr_t const message ) noexcept
        {
            motor::log::global_t::error( message ) ;
            _quitting = true ;
            close() ;
        }

        void_t open_window( size_t const index ) noexcept
        {
            bool_t const d3d = index == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | shared cubes | D3D11" : "motor | shared cubes | GL4" ;
            info.x = int_t( 40 + index * 660 ) ;
            info.y = 90 ;
            info.w = 640 ;
            info.h = 480 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[index] = window_data{} ;
            _windows[index].id = create_window( info ) ;
            send_window_message( _windows[index].id, []( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { true } ) ) ;
            } ) ;
            if( index == 1 ) ++_opens ;
        }

        bool_t all_ready( void_t ) const noexcept
        {
            if( !_windows[0].created || !_windows[0].shader_ready ) return false ;
            return !_options.dual || ( _windows[1].created && _windows[1].shader_ready ) ;
        }

        void_t create_sets( void_t ) noexcept
        {
            // Keep the original fixed 4:3 framing; both windows share these variable sets.
            _camera.make_orthographic( 7.142857f, 5.357143f, 1.0f, 9.0f ) ;
            _camera.look_at( motor::math::vec3f_t( 0.0f, 0.0f, 6.0f ),
                motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), motor::math::vec3f_t( 0.0f ) ) ;
            motor::math::vec4f_t const colors[4] = {
                { 0.15f, 0.80f, 0.35f, 1.0f }, { 1.0f, 0.0f, 1.0f, 1.0f },
                { 0.15f, 0.45f, 1.0f, 1.0f }, { 1.0f, 0.30f, 0.10f, 1.0f } } ;

            for( size_t id = 0 ; id < 4 ; ++id )
            {
                auto vars = motor::shared( motor::graphics::variable_set_t() ) ;
                vars->data_variable<motor::math::mat4f_t>( "u_view" )->set( _camera.get_view_matrix() ) ;
                vars->data_variable<motor::math::mat4f_t>( "u_proj" )->set( _camera.get_proj_matrix() ) ;
                vars->data_variable<motor::math::vec4f_t>( "u_color" )->set( colors[id] ) ;
                auto * world = vars->data_variable<motor::math::mat4f_t>( "u_world" ) ;
                world->set( identity() ) ;
                size_t const assigned = _shader->add_variable_set( motor::move( vars ) ) ;
                if( assigned != id )
                {
                    fail( "[shared scene] unexpected variable-set ID" ) ;
                    return ;
                }
                if( id != 1 )
                {
                    size_t const object = id == 3 ? 0 : ( id == 0 ? 1 : 2 ) ;
                    _objects[object] = object_data{ assigned, world } ;
                }
            }
            _shader->drop_variable_set( 1 ) ;
            _sets_created = true ;
            update_objects() ;
            motor::log::global_t::status( "[shared scene] sets 0, 2, 3 created; ID 1 is a hole; draw order 3, 0, 2" ) ;
        }

        void_t update_objects( void_t ) noexcept
        {
            if( !_sets_created ) return ;
            _objects[0].world->set( world_matrix( -1.70f, -0.25f, 0.55f + _time ) ) ;
            _objects[1].world->set( world_matrix( 0.0f, -0.25f + 0.45f*std::sin( _time ), -0.55f ) ) ;
            _objects[2].world->set( world_matrix( 1.70f, -0.25f, 0.55f ) ) ;
        }

    public:
        shared_scene_app( options const opts, bool_t * passed ) noexcept
            : _options( opts ), _passed( passed ), _want_secondary( opts.dual ) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            // One cube, shared by all objects and both backends.
            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .resize( 24 ).update<vertex>( []( vertex * vertices, size_t )
                    {
                        float_t const positions[8][3] = {
                            {-0.45f,-0.45f,-0.45f}, {0.45f,-0.45f,-0.45f},
                            {0.45f,0.45f,-0.45f}, {-0.45f,0.45f,-0.45f},
                            {-0.45f,-0.45f,0.45f}, {0.45f,-0.45f,0.45f},
                            {0.45f,0.45f,0.45f}, {-0.45f,0.45f,0.45f} } ;
                        uint_t const faces[6][4] = {
                            {4,5,6,7}, {1,0,3,2}, {0,4,7,3},
                            {5,1,2,6}, {3,7,6,2}, {0,1,5,4} } ;
                        float_t const shades[6] = {1.0f,0.50f,0.65f,0.80f,0.92f,0.40f} ;
                        for( size_t face = 0 ; face < 6 ; ++face )
                        {
                            for( size_t corner = 0 ; corner < 4 ; ++corner )
                            {
                                auto const * p = positions[faces[face][corner]] ;
                                vertices[face*4+corner].position = motor::math::vec3f_t( p[0], p[1], p[2] ) ;
                                vertices[face*4+corner].shade = motor::math::vec3f_t( shades[face] ) ;
                            }
                        }
                    } ) ;
                auto ib = motor::graphics::index_buffer_t()
                    .set_layout_element( motor::graphics::type::tuint )
                    .resize( 36 ).update<uint_t>( []( uint_t * indices, size_t )
                    {
                        uint_t const pattern[6] = { 0, 1, 2, 0, 2, 3 } ;
                        for( uint_t face = 0 ; face < 6 ; ++face )
                            for( uint_t i = 0 ; i < 6 ; ++i ) indices[face*6+i] = face*4+pattern[i] ;
                    } ) ;
                _cube = motor::graphics::geometry_object_t( "shared_cube",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }

            {
                motor::graphics::msl_object_t shader( "shared_scene" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config shared_scene
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ;
                            in vec3_t shade : color0 ;
                            out vec4_t pos : position ;
                            out vec3_t shade : texcoord0 ;
                            mat4_t u_world ;
                            mat4_t u_view ;
                            mat4_t u_proj ;
                            void main()
                            {
                                out.pos = u_proj * u_view * u_world * vec4_t( in.pos, 1.0 ) ;
                                out.shade = in.shade ;
                            }
                        }
                        pixel_shader
                        {
                            in vec3_t shade : texcoord0 ;
                            out vec4_t color : color ;
                            vec4_t u_color ;
                            void main()
                            {
                                out.color = vec4_t( u_color.xyz ' in.shade, u_color.w ) ;
                            }
                        }
                    }
                )" ) ;
                _geometry_id = shader.link_geometry( "shared_cube" ) ;
                _shader = motor::shared( std::move( shader ) ) ;
                if( _options.eager ) create_sets() ;
            }

            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.045f, 0.055f, 0.07f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "shared scene state" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _quitting ) return ;
            open_window( 0 ) ;
            if( _options.dual ) open_window( 1 ) ;
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_sets_created && all_ready() ) create_sets() ;
            if( !_options.still && !_paused ) _time += data.sec_dt ;
            update_objects() ;

            if( _options.dual )
            {
                if( _want_secondary && _windows[1].id == invalid ) open_window( 1 ) ;
                else if( !_want_secondary && _windows[1].id != invalid && !_closing_secondary )
                {
                    _closing_secondary = true ;
                    send_window_message( _windows[1].id, []( auto & window )
                    { window.send_message( motor::application::close_message( { true } ) ) ; } ) ;
                }
            }
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            size_t const index = id == _windows[0].id ? 0 : 1 ;
            auto & window = _windows[index] ;
            if( data.first_frame )
            {
                fe->configure<motor::graphics::state_object_t>( &_state ) ;
                fe->configure<motor::graphics::geometry_object_t>( &_cube ) ;
                fe->configure<motor::graphics::msl_object_t>( _shader ) ;
            }
            if( data.last_frame )
            {
                fe->release<motor::graphics::msl_object_t>( _shader ) ;
                fe->release<motor::graphics::geometry_object_t>( &_cube ) ;
                fe->release<motor::graphics::state_object_t>( &_state ) ;
                if( index == 1 )
                {
                    window = window_data{} ;
                    _closing_secondary = false ;
                    ++_closes ;
                    motor::log::global_t::status( "[shared scene] secondary last frame; resource releases queued for this backend" ) ;
                }
                return ;
            }

            auto const status = fe->decode( _shader ) ;
            bool_t const ready = status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok ;
            if( ready && !window.shader_ready )
            {
                if( index == 1 ) ++_ready_count ;
                motor::log::global_t::status( index == 0 ?
                    "[shared scene] primary shader ready" : "[shared scene] secondary shader ready" ) ;
            }
            window.shader_ready = ready ;
            fe->push( &_state ) ;
            if( ready && _sets_created )
            {
                for( auto const & object : _objects )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.geo = _geometry_id ;
                    detail.varset = object.set_id ;
                    fe->render( _shader, detail ) ;
                    ++window.draws ;
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 12 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 270, 95 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Shared scene", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::BeginDisabled( _options.smoke || _closing_secondary ) ;
                if( _options.dual ) ImGui::Checkbox( "D3D11 window", &_want_secondary ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
            if( state.create_changed ) window.created = true ;
            if( !state.close_changed ) return ;
            if( id == _windows[0].id ) { _quitting = true ; close() ; }
            else { _want_secondary = false ; _closing_secondary = true ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
                fail( "[shared scene] smoke timed out" ) ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting || !_sets_created || !_windows[0].shader_ready ) return ;
            if( _windows[0].draws == 0 ) return ;
            if( ++_phase_frames < 60 ) return ;
            if( !_options.dual )
            {
                *_passed = true ;
                _quitting = true ;
                motor::log::global_t::status( "[shared scene] single-backend smoke completed; pixels require visual verification" ) ;
                close() ;
                return ;
            }
            if( ( _phase == 0 || _phase == 2 ) && _windows[1].shader_ready && _windows[1].draws >= 180 )
            {
                _want_secondary = false ;
                ++_phase ;
                _phase_frames = 0 ;
            }
            else if( _phase == 1 && _windows[1].id == invalid )
            {
                _want_secondary = true ;
                ++_phase ;
                _phase_frames = 0 ;
            }
            else if( _phase == 3 && _windows[1].id == invalid )
            {
                *_passed = _opens == 2 && _closes == 2 && _ready_count == 2 ;
                _quitting = true ;
                motor::log::global_t::status( *_passed ?
                    "[shared scene] lifecycle smoke completed; pixels require visual verification" :
                    "[shared scene] lifecycle smoke failed" ) ;
                close() ;
            }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & object : _objects ) object.world = nullptr ;
            motor::release( motor::move( _shader ) ) ;
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
        else if( arg == "--eager" ) opts.eager = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else
        {
            motor::log::global_t::status( "Usage: 03_shared_scene [--gl-only|--d3d-only] [--eager] [--smoke] [--still]" ) ;
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
            motor::shared( sample::shared_scene_app( opts, &passed ) ) ) ;
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
