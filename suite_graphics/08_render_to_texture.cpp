#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/framebuffer_object.h>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
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
        bool_t smoke = false ;
        bool_t still = false ;
        bool_t explicit_state = false ;
    } ;

    class render_to_texture_app : public motor::application::app
    {
        static constexpr size_t invalid = size_t( -1 ) ;
        struct vertex
        {
            motor::math::vec3f_t position ;
            motor::math::vec3f_t shade ;
        } ;
        struct window_data
        {
            window_id_t id = invalid ;
            size_t revision = invalid ;
            size_t draws = 0 ;
            bool_t ready = false ;
        } ;

        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _cube ;
        motor::graphics::geometry_object_t _quad ;
        motor::graphics::framebuffer_object_t _framebuffer ;
        motor::graphics::state_object_t _scene_state ;
        motor::graphics::state_object_t _flat_state ;
        motor::graphics::msl_object_mtr_t _scene = nullptr ;
        motor::graphics::msl_object_mtr_t _markers = nullptr ;
        motor::graphics::msl_object_mtr_t _post = nullptr ;
        motor::graphics::data_variable<motor::math::mat4f_t> * _world[3] = {} ;
        motor::graphics::data_variable<float_t> * _effect = nullptr ;
        motor::gfx::generic_camera_t _camera ;
        int_t _resolution = 1 ;
        int_t _requested_resolution = 1 ;
        int_t _mode = 0 ;
        size_t _revision = 0 ;
        size_t _phase = 0 ;
        size_t _opens = 0 ;
        size_t _closes = 0 ;
        size_t _closed_frames = 0 ;
        bool_t _paused = false ;
        bool_t _explicit_state = false ;
        bool_t _want_secondary = true ;
        bool_t _closing_secondary = false ;
        bool_t _quitting = false ;
        float_t _time = 0.0f ;
        std::chrono::steady_clock::time_point _started ;

        static uint_t target_width( int_t const choice ) noexcept
        {
            return choice == 0 ? 320 : ( choice == 1 ? 640 : 1280 ) ;
        }

        void_t fail( motor::string_cref_t message ) noexcept
        {
            if( _quitting ) return ;
            motor::log::global_t::error( "[RTT] " + message ) ;
            _quitting = true ;
            close() ;
        }

        void_t open_window( size_t const index ) noexcept
        {
            bool_t const d3d = index == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | render to texture | D3D11" : "motor | render to texture | GL4" ;
            info.x = int_t( 40 + index * 660 ) ;
            info.y = 80 ;
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

        bool_t settled( void_t ) const noexcept
        {
            if( _closing_secondary ) return false ;
            for( auto const & window : _windows )
            {
                if( window.id != invalid && ( !window.ready || window.revision != _revision ) ) return false ;
            }
            return _windows[0].ready ;
        }

        template< typename T >
        bool_t ready( motor::graphics::gen4::frontend_ptr_t fe, T * object ) noexcept
        {
            auto const status = fe->decode( object ) ;
            if( status.first != motor::graphics::object_state::ready ) return false ;
            if( status.second == motor::graphics::result::ok ) return true ;
            fail( "configure failed: " + object->name() ) ;
            return false ;
        }

        void_t update_worlds( void_t ) noexcept
        {
            for( size_t i = 0 ; i < 3 ; ++i )
            {
                float_t const angle = 0.45f + float_t( i ) * 0.7f + _time * 0.55f ;
                float_t const c = std::cos( angle ), s = std::sin( angle ) ;
                float_t const cx = std::cos( 0.35f ), sx = std::sin( 0.35f ) ;
                motor::math::mat4f_t world ;
                world.set_column( 0, motor::math::vec4f_t( c, 0.0f, -s, 0.0f ) ) ;
                world.set_column( 1, motor::math::vec4f_t( s*sx, cx, c*sx, 0.0f ) ) ;
                world.set_column( 2, motor::math::vec4f_t( s*cx, -sx, c*cx, 0.0f ) ) ;
                // The front cube is submitted first: the later cubes must not paint over it.
                float_t const x = i == 0 ? 0.25f * std::sin( _time ) : ( i == 1 ? -0.95f : 0.95f ) ;
                float_t const y = i == 0 ? -0.4f : -0.05f ;
                float_t const z = i == 0 ? 1.4f : 0.0f ;
                world.set_column( 3, motor::math::vec4f_t( x, y, z, 1.0f ) ) ;
                _world[i]->set( world ) ;
            }
        }

    public:
        render_to_texture_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ), _paused( opts.still ),
            _explicit_state( opts.explicit_state ), _want_secondary( opts.dual ) {}

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            _camera.make_orthographic( 6.4f, 4.8f, 0.1f, 10.0f ) ;
            _camera.look_at( motor::math::vec3f_t( 0.0f, 0.0f, 8.0f ),
                motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), motor::math::vec3f_t( 0.0f ) ) ;

            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 ) ;
                vb.resize( 24 ).update<vertex>( []( vertex * ptr, size_t )
                {
                    float_t const p[8][3] = {
                        {-0.7f,-0.7f,-0.7f}, {0.7f,-0.7f,-0.7f}, {0.7f,0.7f,-0.7f}, {-0.7f,0.7f,-0.7f},
                        {-0.7f,-0.7f,0.7f}, {0.7f,-0.7f,0.7f}, {0.7f,0.7f,0.7f}, {-0.7f,0.7f,0.7f} } ;
                    uint_t const faces[6][4] = {
                        {4,5,6,7}, {1,0,3,2}, {0,4,7,3}, {5,1,2,6}, {3,7,6,2}, {0,1,5,4} } ;
                    float_t const shades[6] = { 1.0f, 0.45f, 0.65f, 0.8f, 0.92f, 0.35f } ;
                    for( size_t f = 0 ; f < 6 ; ++f )
                    {
                        for( size_t v = 0 ; v < 4 ; ++v )
                        {
                            auto const * pos = p[faces[f][v]] ;
                            ptr[f*4+v] = { motor::math::vec3f_t( pos[0], pos[1], pos[2] ),
                                motor::math::vec3f_t( shades[f] ) } ;
                        }
                    }
                } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
                ib.resize( 36 ).update<uint_t>( []( uint_t * ptr, size_t )
                {
                    uint_t const pattern[6] = { 0, 1, 2, 0, 2, 3 } ;
                    for( uint_t f = 0 ; f < 6 ; ++f )
                        for( size_t i = 0 ; i < 6 ; ++i ) ptr[f*6+i] = f*4+pattern[i] ;
                } ) ;
                _cube = motor::graphics::geometry_object_t( "rtt_cube",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                auto vb = motor::graphics::vertex_buffer_t().add_layout_element(
                    motor::graphics::vertex_attribute::position, motor::graphics::type::tfloat,
                    motor::graphics::type_struct::vec2 ) ;
                vb.resize( 4 ).update<motor::math::vec2f_t>( []( auto * ptr, size_t )
                {
                    ptr[0] = motor::math::vec2f_t( -1.0f, -1.0f ) ;
                    ptr[1] = motor::math::vec2f_t( 1.0f, -1.0f ) ;
                    ptr[2] = motor::math::vec2f_t( 1.0f, 1.0f ) ;
                    ptr[3] = motor::math::vec2f_t( -1.0f, 1.0f ) ;
                } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
                ib.resize( 6 ).update<uint_t>( []( uint_t * ptr, size_t )
                {
                    uint_t const indices[6] = { 0, 1, 2, 0, 2, 3 } ;
                    for( size_t i = 0 ; i < 6 ; ++i ) ptr[i] = indices[i] ;
                } ) ;
                _quad = motor::graphics::geometry_object_t( "rtt_quad",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                _framebuffer = motor::graphics::framebuffer_object_t( "rtt_scene" ) ;
                _framebuffer.set_target( motor::graphics::color_target_type::rgba_uint_8 )
                    .set_target( motor::graphics::depth_stencil_target_type::depth32 ).resize( 640, 480 ) ;

                _scene_state = motor::graphics::state_object_t( "rtt_scene_state" ) ;
                for( int_t i = 0 ; i < 6 ; ++i )
                {
                    motor::graphics::render_state_sets_t state ;
                    state.depth_s.do_change = true ;
                    state.depth_s.ss.do_activate = true ;
                    state.depth_s.ss.do_depth_write = true ;
                    state.polygon_s.do_change = true ;
                    state.polygon_s.ss.do_activate = false ;
                    state.clear_s.do_change = true ;
                    state.clear_s.ss.do_activate = i < 3 ;
                    state.clear_s.ss.do_color_clear = true ;
                    state.clear_s.ss.do_depth_clear = true ;
                    state.clear_s.ss.clear_color = motor::math::vec4f_t( 0.04f, 0.05f, 0.06f, 1.0f ) ;
                    state.view_s.do_change = true ;
                    state.view_s.ss.do_activate = true ;
                    state.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, target_width( i % 3 ), target_width( i % 3 ) * 3 / 4 ) ;
                    _scene_state.add_render_state_set( state ) ;
                }
                motor::graphics::render_state_sets_t state ;
                state.depth_s.do_change = true ;
                state.depth_s.ss.do_activate = false ;
                state.depth_s.ss.do_depth_write = false ;
                state.polygon_s.do_change = true ;
                state.polygon_s.ss.do_activate = false ;
                state.clear_s.do_change = true ;
                state.clear_s.ss.do_activate = false ;
                _flat_state = motor::graphics::state_object_t( "rtt_flat_state" ) ;
                _flat_state.add_render_state_set( state ) ;
            }
            {
                motor::graphics::msl_object_t shader( "rtt_cubes" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config rtt_cubes
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ;
                            in vec3_t shade : color0 ;
                            out vec4_t pos : position ;
                            out vec3_t shade : texcoord0 ;
                            mat4_t u_world ;
                            mat4_t u_view_projection ;
                            void main()
                            {
                                out.pos = u_view_projection * u_world * vec4_t( in.pos, 1.0 ) ;
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
                                out.color = vec4_t( u_color.xyz ' in.shade, 1.0 ) ;
                            }
                        }
                    }
                )" ) ;
                shader.link_geometry( "rtt_cube" ) ;
                motor::math::vec4f_t const colors[3] = {
                    { 0.95f, 0.35f, 0.1f, 1.0f }, { 0.2f, 0.8f, 0.45f, 1.0f }, { 0.2f, 0.45f, 1.0f, 1.0f } } ;
                for( size_t i = 0 ; i < 3 ; ++i )
                {
                    motor::graphics::variable_set_t vars ;
                    _world[i] = vars.data_variable<motor::math::mat4f_t>( "u_world" ) ;
                    vars.data_variable<motor::math::mat4f_t>( "u_view_projection" )->set(
                        _camera.get_proj_matrix() * _camera.get_view_matrix() ) ;
                    vars.data_variable<motor::math::vec4f_t>( "u_color" )->set( colors[i] ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _scene = motor::shared( std::move( shader ) ) ;
                update_worlds() ;
            }
            // Markers are written into the offscreen image, never added by the presentation shader.
            {
                motor::graphics::msl_object_t shader( "rtt_markers" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config rtt_markers
                    {
                        vertex_shader
                        {
                            in vec2_t pos : position ;
                            out vec4_t pos : position ;
                            out vec2_t uv : texcoord0 ;
                            void main()
                            {
                                out.pos = vec4_t( in.pos, 0.0, 1.0 ) ;
                                out.uv = in.pos * 0.5 + 0.5 ;
                            }
                        }
                        pixel_shader
                        {
                            in vec2_t uv : texcoord0 ;
                            out vec4_t color : color ;
                            void main()
                            {
                                vec3_t color = vec3_t( 0.055, 0.065, 0.075 ) ;
                                if( in.uv.x < 0.12 && in.uv.y > 0.84 ) color = vec3_t( 1.0, 0.1, 0.15 ) ;
                                if( in.uv.x > 0.88 && in.uv.y > 0.84 ) color = vec3_t( 0.15, 1.0, 0.25 ) ;
                                if( in.uv.x < 0.12 && in.uv.y < 0.16 ) color = vec3_t( 0.1, 0.35, 1.0 ) ;
                                if( in.uv.x > 0.88 && in.uv.y < 0.16 ) color = vec3_t( 1.0, 0.8, 0.1 ) ;
                                if( in.uv.y > 0.96 && in.uv.x > 0.2 && in.uv.x < 0.8 ) color = vec3_t( 0.95, 0.95, 0.95 ) ;
                                out.color = vec4_t( color, 1.0 ) ;
                            }
                        }
                    }
                )" ) ;
                shader.link_geometry( "rtt_quad" ) ;
                shader.add_variable_set( motor::shared( motor::graphics::variable_set_t() ) ) ;
                _markers = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "rtt_post" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config rtt_post
                    {
                        vertex_shader
                        {
                            in vec2_t pos : position ;
                            out vec4_t pos : position ;
                            out vec2_t uv : texcoord0 ;
                            void main()
                            {
                                out.pos = vec4_t( in.pos, 0.0, 1.0 ) ;
                                out.uv = in.pos * 0.5 + 0.5 ;
                            }
                        }
                        pixel_shader
                        {
                            in vec2_t uv : texcoord0 ;
                            out vec4_t color : color ;
                            tex2d_t u_scene ;
                            float_t u_effect ;
                            void main()
                            {
                                vec3_t color = rt_texture( u_scene, in.uv ).xyz ;
                                if( u_effect > 0.5 && u_effect < 1.5 )
                                {
                                    float_t grey = dot( color, vec3_t( 0.2126, 0.7152, 0.0722 ) ) ;
                                    color = vec3_t( grey, grey, grey ) ;
                                }
                                if( u_effect > 1.5 ) color = vec3_t( 1.0, 1.0, 1.0 ) - color ;
                                out.color = vec4_t( color, 1.0 ) ;
                            }
                        }
                    }
                )" ) ;
                shader.link_geometry( "rtt_quad" ) ;
                motor::graphics::variable_set_t vars ;
                vars.texture_variable( "u_scene" )->set( "rtt_scene.0" ) ;
                _effect = vars.data_variable<float_t>( "u_effect" ) ;
                _effect->set( 0.0f ) ;
                shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                _post = motor::shared( std::move( shader ) ) ;
            }
            open_window( 0 ) ;
            if( _options.dual ) open_window( 1 ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused ) _time += data.sec_dt ;
            update_worlds() ;
            _effect->set( float_t( _mode ) ) ;
            if( _requested_resolution != _resolution && settled() )
            {
                _resolution = _requested_resolution ;
                uint_t const width = target_width( _resolution ) ;
                _framebuffer.resize( width, width * 3 / 4 ) ;
                ++_revision ;
                for( auto & window : _windows ) { window.ready = false ; window.draws = 0 ; }
                motor::log::global_t::status( "[RTT] target resize: " + motor::to_string( width ) +
                    "x" + motor::to_string( width * 3 / 4 ) ) ;
            }
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
            if( data.last_frame )
            {
                fe->release<motor::graphics::msl_object_t>( _post ) ;
                fe->release<motor::graphics::msl_object_t>( _markers ) ;
                fe->release<motor::graphics::msl_object_t>( _scene ) ;
                fe->release<motor::graphics::framebuffer_object_t>( &_framebuffer ) ;
                fe->release<motor::graphics::geometry_object_t>( &_quad ) ;
                fe->release<motor::graphics::geometry_object_t>( &_cube ) ;
                fe->release<motor::graphics::state_object_t>( &_flat_state ) ;
                fe->release<motor::graphics::state_object_t>( &_scene_state ) ;
                if( index == 1 )
                {
                    window = window_data{} ;
                    _closing_secondary = false ;
                    ++_closes ;
                    motor::log::global_t::status( "[RTT] secondary last frame; releases queued" ) ;
                }
                return ;
            }
            if( data.first_frame )
            {
                fe->configure<motor::graphics::state_object_t>( &_scene_state ) ;
                fe->configure<motor::graphics::state_object_t>( &_flat_state ) ;
                fe->configure<motor::graphics::geometry_object_t>( &_cube ) ;
                fe->configure<motor::graphics::geometry_object_t>( &_quad ) ;
                fe->configure<motor::graphics::framebuffer_object_t>( &_framebuffer ) ;
                fe->configure<motor::graphics::msl_object_t>( _scene ) ;
                fe->configure<motor::graphics::msl_object_t>( _markers ) ;
                fe->configure<motor::graphics::msl_object_t>( _post ) ;
                window.revision = _revision ;
            }
            else if( window.revision != _revision )
            {
                // Only the framebuffer changes; the shader and its texture name stay intact.
                if( !fe->configure<motor::graphics::framebuffer_object_t>( &_framebuffer ) )
                { fail( "framebuffer reconfigure was rejected" ) ; return ; }
                window.revision = _revision ;
            }
            window.ready = ready( fe, &_scene_state ) && ready( fe, &_flat_state ) &&
                ready( fe, &_cube ) && ready( fe, &_quad ) && ready( fe, &_framebuffer ) &&
                ready( fe, _scene ) && ready( fe, _markers ) && ready( fe, _post ) ;
            if( !window.ready || _quitting ) return ;

            fe->use( &_framebuffer ) ;
            fe->push( &_scene_state, size_t( _resolution ) ) ;
            {
                fe->push( &_flat_state ) ;
                fe->render( _markers, motor::graphics::gen4::backend_t::render_detail_t{} ) ;
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
                // Diagnostic reference: explicitly set depth/viewport instead of relying on pop restoration.
                if( _explicit_state ) fe->push( &_scene_state, size_t( _resolution + 3 ) ) ;
                for( size_t i = 0 ; i < 3 ; ++i )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = i ;
                    fe->render( _scene, detail ) ;
                }
                if( _explicit_state ) fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            fe->unuse( motor::graphics::gen4::backend::unuse_type::framebuffer ) ;
            fe->push( &_flat_state ) ;
            fe->render( _post, motor::graphics::gen4::backend_t::render_detail_t{} ) ;
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            ++window.draws ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 100, 35 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 290, 155 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Render to texture", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::Text( "Target: %u x %u", target_width( _resolution ), target_width( _resolution ) * 3 / 4 ) ;
                ImGui::BeginDisabled( _options.smoke || !settled() ) ;
                ImGui::Combo( "Resolution", &_requested_resolution, "320 x 240\0" "640 x 480\0" "1280 x 960\0" ) ;
                ImGui::Combo( "Effect", &_mode, "Original\0" "Greyscale\0" "Inverted\0" ) ;
                ImGui::Checkbox( "Explicit cube state", &_explicit_state ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                if( _options.dual )
                {
                    ImGui::SameLine() ;
                    ImGui::Checkbox( "D3D11 window", &_want_secondary ) ;
                }
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( !state.close_changed ) return ;
            if( id == _windows[0].id ) { _quitting = true ; close() ; }
            else { _want_secondary = false ; _closing_secondary = true ; }
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            if( _phase == 3 )
            {
                if( _windows[1].id != invalid || ++_closed_frames < 30 ) return ;
                _want_secondary = true ;
                _phase = 4 ;
                for( auto & window : _windows ) window.draws = 0 ;
                return ;
            }
            if( !settled() || _requested_resolution != _resolution ) return ;
            for( auto const & window : _windows )
                if( window.id != invalid && window.draws < 90 ) return ;
            if( _options.dual && ( _windows[1].id == invalid || !_windows[1].ready ) ) return ;
            motor::log::global_t::status( "[RTT] completed phase " + motor::to_string( _phase ) ) ;
            for( auto & window : _windows ) window.draws = 0 ;
            if( _phase == 0 ) { _requested_resolution = 0 ; _mode = 1 ; _phase = 1 ; }
            else if( _phase == 1 ) { _requested_resolution = 2 ; _mode = 2 ; _phase = 2 ; }
            else if( _phase == 2 && _options.dual ) { _want_secondary = false ; _phase = 3 ; }
            else if( _phase == 2 || _phase == 4 ) { _requested_resolution = 1 ; _mode = 0 ; _phase = 5 ; }
            else
            {
                *_passed = !_options.dual || ( _opens == 2 && _closes == 1 ) ;
                _quitting = true ;
                motor::log::global_t::status( *_passed ?
                    "[RTT] smoke passed; image correctness requires visual verification" : "[RTT] lifecycle counts failed" ) ;
                close() ;
            }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 60 ) )
                fail( "smoke timed out in phase " + motor::to_string( _phase ) ) ;
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & world : _world ) world = nullptr ;
            _effect = nullptr ;
            motor::release( motor::move( _post ) ) ;
            motor::release( motor::move( _markers ) ) ;
            motor::release( motor::move( _scene ) ) ;
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
        else if( arg == "--explicit-state" ) opts.explicit_state = true ;
        else
        {
            motor::log::global_t::status( "Usage: 08_render_to_texture [--gl-only|--d3d-only] [--smoke] [--still] [--explicit-state]" ) ;
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
            motor::shared( sample::render_to_texture_app( opts, &passed ) ) ) ;
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
