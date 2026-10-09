#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/streamout_object.h>
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
#include <motor/std/vector>

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

    class transform_feedback_app : public motor::application::app
    {
        struct vertex
        {
            motor::math::vec4f_t position ;
            motor::math::vec4f_t color ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            size_t draws = 0 ;
            float_t aspect = 1.2f ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::data_variable<motor::math::vec4f_t> * motion[2] = { nullptr, nullptr } ;
            motor::graphics::data_variable<motor::math::vec4f_t> * plane = nullptr ;
            motor::graphics::data_variable<motor::math::mat4f_t> * matrix = nullptr ;
            motor::graphics::data_variable<motor::math::mat4f_t> * plane_matrix = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * plane_origin = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * plane_u = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * plane_v = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * plane_normal = nullptr ;
        } ;

        static constexpr size_t side = 50 ;
        static constexpr size_t count = side * side * side ;
        // Cube width is 0.21: leave another full cube width between neighboring faces.
        static constexpr float_t spacing = 0.42f ;
        static constexpr float_t half_extent = float_t( side - 1 ) * spacing * 0.5f ;
        static constexpr float_t scene_scale = half_extent / 2.2f ;
        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _input ;
        motor::graphics::geometry_object_t _plane_geometry ;
        motor::graphics::streamout_object_t _output ;
        motor::graphics::msl_object_mtr_t _writers[2] = { nullptr, nullptr } ;
        motor::graphics::msl_object_mtr_t _display = nullptr ;
        motor::graphics::msl_object_mtr_t _plane_shader = nullptr ;
        motor::graphics::state_object_t _state ;
        size_t _phase = 0 ;
        size_t _expected = count ;
        int_t _mode = 3 ;
        int_t _frame_mode = 3 ;
        float_t _time = 0.0f ;
        float_t _angle = 25.0f ;
        float_t _tilt = 25.0f ;
        float_t _offset = 0.0f ;
        float_t _camera_yaw = 38.0f ;
        bool_t _show_plane = true ;
        bool_t _frame_show_plane = true ;
        bool_t _paused = false ;
        bool_t _quitting = false ;
        std::chrono::steady_clock::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }

        static motor::math::vec3f_t position( size_t const i ) noexcept
        {
            float_t const half = float_t( side - 1 ) * 0.5f ;
            return motor::math::vec3f_t( ( float_t( i % side ) - half ) * spacing,
                ( float_t( ( i / side ) % side ) - half ) * spacing,
                ( float_t( i / ( side * side ) ) - half ) * spacing ) ;
        }

        void_t update_variables( void_t ) noexcept
        {
            _frame_mode = _mode ;
            _frame_show_plane = _show_plane && _mode == 3 ;
            float_t const radians = _angle * 0.01745329252f ;
            float_t const tilt = _tilt * 0.01745329252f ;
            motor::math::vec3f_t const normal( std::cos( tilt ) * std::cos( radians ),
                std::sin( tilt ), std::cos( tilt ) * std::sin( radians ) ) ;
            float_t const offset = _mode == 1 ? 4.0f * half_extent :
                ( _mode == 2 ? -4.0f * half_extent : _offset ) ;
            motor::math::vec4f_t const plane( normal, offset ) ;
            // Choose a nonparallel helper so the plane frame remains valid near vertical normals.
            auto helper = std::fabs( normal.y() ) > 0.95f ? motor::math::vec3f_t( 0.0f, 0.0f, 1.0f ) :
                motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ) ;
            auto const u = helper.cross( normal ).normalized() ;
            auto const v = motor::math::vec3f_t( normal ).cross( u ).normalized() ;
            // Reference count only: never used to size the GPU draw or to read back the output.
            _expected = 0 ;
            for( size_t i = 0 ; i < count ; ++i )
            {
                auto const p = position( i ) ;
                if( _mode == 0 || normal.dot( p ) <= offset ) ++_expected ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto & window = _windows[w] ;
                for( auto * motion : window.motion )
                    motion->set( motor::math::vec4f_t( _time, 0.22f, 0.0f, 0.0f ) ) ;
                window.plane->set( plane ) ;
                window.plane_origin->set( motor::math::vec4f_t( normal * _offset, 1.0f ) ) ;
                window.plane_u->set( motor::math::vec4f_t( u, 0.0f ) ) ;
                window.plane_v->set( motor::math::vec4f_t( v, 0.0f ) ) ;
                window.plane_normal->set( motor::math::vec4f_t( normal, 0.0f ) ) ;
                float_t const width = 11.0f * scene_scale * ( window.aspect > 1.0f ? window.aspect : 1.0f ) ;
                float_t const height = 11.0f * scene_scale / ( window.aspect < 1.0f ? window.aspect : 1.0f ) ;
                float_t const yaw = _camera_yaw * 0.01745329252f ;
                motor::math::vec3f_t const target( 0.0f, 1.1f * scene_scale, 0.0f ) ;
                motor::math::vec3f_t const eye = target + motor::math::vec3f_t(
                    30.0f * 0.8660254f * std::sin( yaw ), 15.0f, 30.0f * 0.8660254f * std::cos( yaw ) ) * scene_scale ;
                window.camera.make_orthographic( width, height, 0.1f, 45.0f * scene_scale ) ;
                window.camera.look_at( eye, motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), target ) ;
                auto const matrix = window.camera.get_proj_matrix() * window.camera.get_view_matrix() ;
                window.matrix->set( matrix ) ;
                window.plane_matrix->set( matrix ) ;
            }
        }

        void_t open_window( size_t const w ) noexcept
        {
            bool_t const d3d = w == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | stream output | D3D11" : "motor | transform feedback | GL4" ;
            info.x = int_t( 30 + w * 680 ) ;
            info.y = 60 ;
            info.w = 660 ;
            info.h = 560 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[w].aspect = float_t( info.w ) / float_t( info.h ) ;
            _windows[w].id = create_window( info ) ;
            send_window_message( _windows[w].id, []( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { true } ) ) ;
            } ) ;
        }

    public:
        transform_feedback_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ), _paused( opts.still ) {}

        // Existing resize helpers calculate a ratio with an initial element count of zero.
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                vb.resize( count ).update<vertex>( []( vertex * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i )
                    {
                        float_t const x = float_t( i % side ) / float_t( side - 1 ) ;
                        float_t const y = float_t( ( i / side ) % side ) / float_t( side - 1 ) ;
                        float_t const z = float_t( i / ( side * side ) ) / float_t( side - 1 ) ;
                        ptr[i] = { motor::math::vec4f_t( position( i ), 1.0f ),
                            motor::math::vec4f_t( 0.15f + 0.8f*x, 0.3f + 0.6f*y, 0.25f + 0.65f*z, 1.0f ) } ;
                    }
                } ) ;
                _input = motor::graphics::geometry_object_t( "tf_input",
                    motor::graphics::primitive_type::points, std::move( vb ) ) ;
                auto layout = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                _output = motor::graphics::streamout_object_t( "tf_output", std::move( layout ) ) ;
                _output.resize( count ) ;
            }
            {
                motor::string_t const vertex_stage = R"(
                    vertex_shader
                    {
                        inout vec4_t pos : position ;
                        inout vec4_t color : color ;
                        vec4_t u_motion ;
                        void main()
                        {
                            float_t wave = sin( in.pos.x * 2.0 + in.pos.y * 1.3 + in.pos.z + u_motion.x ) ;
                            out.pos = in.pos ;
                            out.color = vec4_t( in.color.xyz * ( 0.78 + u_motion.y * wave ), 1.0 ) ;
                        }
                    }
                )" ;
                motor::string_t const filter_stage = R"(
                    geometry_shader
                    {
                        in points ;
                        out points[ max_verts = 1 ] ;
                        inout vec4_t pos : position ;
                        inout vec4_t color : color ;
                        vec4_t u_plane ;
                        void main()
                        {
                            for( int i = 0 ; i < in.length() ; ++i )
                            {
                                float_t distance = dot( u_plane.xyz, in[i].pos.xyz ) ;
                                if( distance <= u_plane.w )
                                {
                                    out.pos = in[i].pos ;
                                    out.color = in[i].color ;
                                    emit_vertex() ;
                                }
                            }
                            end_primitive() ;
                        }
                    }
                )" ;
                for( size_t s = 0 ; s < 2 ; ++s )
                {
                    motor::string_t const name = s == 0 ? "tf_vertex_capture" : "tf_filter_capture" ;
                    motor::graphics::msl_object_t shader( name ) ;
                    // No pixel shader: capture only; the backend disables rasterization.
                    shader.add( motor::graphics::msl_api_type::msl_4_0,
                        "config " + name + " { " + vertex_stage + ( s == 1 ? filter_stage : "" ) + " }" ) ;
                    shader.link_geometry( "tf_input" ) ;
                    for( size_t w = 0 ; w < window_count() ; ++w )
                    {
                        motor::graphics::variable_set_t vars ;
                        _windows[w].motion[s] = vars.data_variable<motor::math::vec4f_t>( "u_motion" ) ;
                        if( s == 1 ) _windows[w].plane = vars.data_variable<motor::math::vec4f_t>( "u_plane" ) ;
                        shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                    }
                    _writers[s] = motor::shared( std::move( shader ) ) ;
                }
            }
            {
                motor::graphics::msl_object_t shader( "tf_display" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config tf_display
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            void main() { out.pos = in.pos ; out.color = in.color ; }
                        }
                        geometry_shader
                        {
                            in points ;
                            out triangles[ max_verts = 24 ] ;
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            mat4_t u_view_projection ;
                            void main()
                            {
                                for( int i = 0 ; i < in.length() ; ++i )
                                {
                                    for( int face = 0 ; face < 6 ; ++face )
                                    {
                                        vec3_t normal = vec3_t( 0.0, 0.0, 1.0 ) ;
                                        vec3_t u = vec3_t( 1.0, 0.0, 0.0 ) ;
                                        vec3_t v = vec3_t( 0.0, 1.0, 0.0 ) ;
                                        if( face == 1 ) { normal = vec3_t( 0.0, 0.0, -1.0 ) ; u = vec3_t( -1.0, 0.0, 0.0 ) ; }
                                        if( face == 2 ) { normal = vec3_t( 1.0, 0.0, 0.0 ) ; u = vec3_t( 0.0, 0.0, -1.0 ) ; }
                                        if( face == 3 ) { normal = vec3_t( -1.0, 0.0, 0.0 ) ; u = vec3_t( 0.0, 0.0, 1.0 ) ; }
                                        if( face == 4 ) { normal = vec3_t( 0.0, 1.0, 0.0 ) ; u = vec3_t( 1.0, 0.0, 0.0 ) ; v = vec3_t( 0.0, 0.0, -1.0 ) ; }
                                        if( face == 5 ) { normal = vec3_t( 0.0, -1.0, 0.0 ) ; u = vec3_t( 1.0, 0.0, 0.0 ) ; v = vec3_t( 0.0, 0.0, 1.0 ) ; }
                                        float_t light = 0.28 + 0.72 * max( dot( normal, normalize( vec3_t( -0.4, 0.8, 0.6 ) ) ), 0.0 ) ;
                                        vec3_t center = in[i].pos.xyz + normal * 0.105 ;
                                        out.pos = u_view_projection * vec4_t( center + ( -u - v ) * 0.105, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( u - v ) * 0.105, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( -u + v ) * 0.105, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( u + v ) * 0.105, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        end_primitive() ;
                                    }
                                }
                            }
                        }
                        pixel_shader
                        {
                            inout vec4_t color : color ;
                            void main() { out.color = in.color ; }
                        }
                    }
                )" ) ;
                shader.link_geometry( "tf_input", "tf_output" ) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    motor::graphics::variable_set_t vars ;
                    _windows[w].matrix = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _display = motor::shared( std::move( shader ) ) ;
            }
            {
                // A finite grid visualizes the infinite cutting plane; the arrow points into the rejected half-space.
                motor::vector<vertex> vertices ;
                vertices.reserve( 78 ) ;
                auto add_line = [&]( motor::math::vec3f_t const & a, motor::math::vec3f_t const & b,
                    motor::math::vec4f_t const & color )
                {
                    vertices.push_back( { motor::math::vec4f_t( a * scene_scale, 1.0f ), color } ) ;
                    vertices.push_back( { motor::math::vec4f_t( b * scene_scale, 1.0f ), color } ) ;
                } ;
                for( size_t i = 0 ; i <= 16 ; ++i )
                {
                    float_t const p = -3.8f + 7.6f * float_t( i ) / 16.0f ;
                    motor::math::vec4f_t const color = i == 0 || i == 8 || i == 16 ?
                        motor::math::vec4f_t( 1.0f, 0.8f, 0.2f, 1.0f ) : motor::math::vec4f_t( 0.5f, 0.42f, 0.16f, 1.0f ) ;
                    add_line( motor::math::vec3f_t( p, -3.8f, 0.0f ), motor::math::vec3f_t( p, 3.8f, 0.0f ), color ) ;
                    add_line( motor::math::vec3f_t( -3.8f, p, 0.0f ), motor::math::vec3f_t( 3.8f, p, 0.0f ), color ) ;
                }
                motor::math::vec4f_t const arrow_color( 1.0f, 0.35f, 0.18f, 1.0f ) ;
                motor::math::vec3f_t const tip( 0.0f, 0.0f, 0.9f ) ;
                add_line( motor::math::vec3f_t( 0.0f ), tip, arrow_color ) ;
                add_line( motor::math::vec3f_t( -0.15f, 0.0f, 0.65f ), tip, arrow_color ) ;
                add_line( motor::math::vec3f_t( 0.15f, 0.0f, 0.65f ), tip, arrow_color ) ;
                add_line( motor::math::vec3f_t( 0.0f, -0.15f, 0.65f ), tip, arrow_color ) ;
                add_line( motor::math::vec3f_t( 0.0f, 0.15f, 0.65f ), tip, arrow_color ) ;
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                vb.resize( vertices.size() ).update<vertex>( [&]( vertex * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = vertices[i] ;
                } ) ;
                _plane_geometry = motor::graphics::geometry_object_t( "tf_plane_grid",
                    motor::graphics::primitive_type::lines, std::move( vb ) ) ;
                motor::graphics::msl_object_t shader( "tf_plane" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config tf_plane
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            mat4_t u_view_projection ;
                            vec4_t u_origin ;
                            vec4_t u_axis_u ;
                            vec4_t u_axis_v ;
                            vec4_t u_normal ;
                            void main()
                            {
                                vec3_t p = u_origin.xyz + u_axis_u.xyz * in.pos.x + u_axis_v.xyz * in.pos.y + u_normal.xyz * in.pos.z ;
                                out.pos = u_view_projection * vec4_t( p, 1.0 ) ;
                                out.color = in.color ;
                            }
                        }
                        pixel_shader
                        {
                            inout vec4_t color : color ;
                            void main() { out.color = in.color ; }
                        }
                    }
                )" ) ;
                shader.link_geometry( "tf_plane_grid" ) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    motor::graphics::variable_set_t vars ;
                    auto & window = _windows[w] ;
                    window.plane_matrix = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    window.plane_origin = vars.data_variable<motor::math::vec4f_t>( "u_origin" ) ;
                    window.plane_u = vars.data_variable<motor::math::vec4f_t>( "u_axis_u" ) ;
                    window.plane_v = vars.data_variable<motor::math::vec4f_t>( "u_axis_v" ) ;
                    window.plane_normal = vars.data_variable<motor::math::vec4f_t>( "u_normal" ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _plane_shader = motor::shared( std::move( shader ) ) ;
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
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.025f, 0.035f, 0.045f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "tf_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) { _mode = 0 ; _angle = 0.0f ; _tilt = 0.0f ; _paused = true ; }
            update_variables() ;
            for( size_t w = 0 ; w < window_count() ; ++w ) open_window( w ) ;
            motor::log::global_t::status( "[transform feedback] " + motor::to_string( side ) + "x" +
                motor::to_string( side ) + "x" + motor::to_string( side ) + " centers (" +
                motor::to_string( count ) + "); spacing 0.42, cube width 0.21; up to " +
                motor::to_string( count * 12 ) + " cube triangles per window" ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused )
            {
                _time += data.sec_dt ;
                _angle = std::fmod( _angle + 18.0f * data.sec_dt, 360.0f ) ;
                _tilt = 25.0f + 35.0f * std::sin( _time * 0.35f ) ;
                _offset = 1.4f * scene_scale * std::sin( _time * 0.65f ) ;
            }
            update_variables() ;
        }

        void_t on_first_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_input ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_plane_geometry ) ;
            fe->configure<motor::graphics::streamout_object_t>( &_output ) ;
            for( auto shader : _writers ) fe->configure<motor::graphics::msl_object_t>( shader ) ;
            fe->configure<motor::graphics::msl_object_t>( _display ) ;
            fe->configure<motor::graphics::msl_object_t>( _plane_shader ) ;
        }

        void_t on_last_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>( _plane_shader ) ;
            fe->release<motor::graphics::msl_object_t>( _display ) ;
            for( auto shader : _writers ) fe->release<motor::graphics::msl_object_t>( shader ) ;
            fe->release<motor::graphics::streamout_object_t>( &_output ) ;
            fe->release<motor::graphics::geometry_object_t>( &_input ) ;
            fe->release<motor::graphics::geometry_object_t>( &_plane_geometry ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            size_t const w = id == _windows[0].id ? 0 : 1 ;
            fe->push( &_state ) ;
            bool_t ready = true ;
            motor::graphics::msl_object_mtr_t const shaders[4] = { _writers[0], _writers[1], _display, _plane_shader } ;
            for( auto shader : shaders )
            {
                auto const status = fe->decode( shader ) ;
                ready = ready && status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok ;
            }
            if( ready && !_quitting )
            {
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = w ;
                    detail.num_elems = count ;
                    fe->use( &_output ) ;
                    fe->render( _writers[_frame_mode == 0 ? 0 : 1], detail ) ;
                    fe->unuse( motor::graphics::gen4::backend::unuse_type::streamout ) ;
                }
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = w ;
                    detail.feed_from_streamout = true ;
                    detail.use_streamout_count = true ;
                    fe->render( _display, detail ) ;
                }
                if( _frame_show_plane )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = w ;
                    fe->render( _plane_shader, detail ) ;
                }
                ++_windows[w].draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 44 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 360, 280 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Transform feedback", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::Text( "%zu x %zu x %zu | %zu input centers", side, side, side, count ) ;
                ImGui::Text( "CPU expected: %zu cubes", _expected ) ;
                ImGui::BeginDisabled( _options.smoke ) ;
                ImGui::Combo( "Mode", &_mode, "VS capture\0GS keep all\0GS keep none\0GS moving cut\0" ) ;
                ImGui::SliderFloat( "Angle", &_angle, 0.0f, 360.0f ) ;
                ImGui::SliderFloat( "Tilt", &_tilt, -90.0f, 90.0f ) ;
                ImGui::SliderFloat( "Offset", &_offset, -4.0f * scene_scale, 4.0f * scene_scale ) ;
                ImGui::SliderFloat( "Camera", &_camera_yaw, 0.0f, 360.0f ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::SameLine() ;
                ImGui::Checkbox( "Plane", &_show_plane ) ;
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w ) if( _windows[w].draws < 90 ) return ;
            motor::log::global_t::status( "[transform feedback] phase " + motor::to_string( _phase ) +
                ", mode " + motor::to_string( _frame_mode ) + ", CPU expected " + motor::to_string( _expected ) +
                "; capture/display submitted on every backend" ) ;
            if( ++_phase == 9 )
            {
                *_passed = true ;
                _quitting = true ;
                motor::log::global_t::status( "[transform feedback] smoke passed; GPU contents/count need visual verification" ) ;
                close() ;
                return ;
            }
            int_t const modes[9] = { 0, 1, 3, 2, 1, 3, 3, 3, 0 } ;
            _mode = modes[_phase] ;
            _angle = _phase == 5 ? 90.0f : ( _phase == 7 ? 37.0f : 0.0f ) ;
            _tilt = _phase == 6 ? 90.0f : ( _phase == 7 ? 28.0f : 0.0f ) ;
            _offset = 0.0f ;
            for( auto & window : _windows ) window.draws = 0 ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 120 ) )
            {
                _quitting = true ;
                motor::log::global_t::error( "[transform feedback] smoke timed out" ) ;
                close() ;
            }
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.resize_changed && state.resize_msg.resize && state.resize_msg.w > 0 && state.resize_msg.h > 0 )
                _windows[id == _windows[0].id ? 0 : 1].aspect = float_t( state.resize_msg.w ) / float_t( state.resize_msg.h ) ;
            if( state.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & window : _windows )
            {
                for( auto & motion : window.motion ) motion = nullptr ;
                window.plane = nullptr ; window.matrix = nullptr ;
                window.plane_matrix = nullptr ; window.plane_origin = nullptr ;
                window.plane_u = nullptr ; window.plane_v = nullptr ; window.plane_normal = nullptr ;
            }
            motor::release( motor::move( _display ) ) ;
            motor::release( motor::move( _plane_shader ) ) ;
            for( auto & shader : _writers ) motor::release( motor::move( shader ) ) ;
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
            motor::log::global_t::status( "Usage: 10_transform_feedback [--gl-only|--d3d-only] [--smoke] [--still]" ) ;
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
            motor::shared( sample::transform_feedback_app( opts, &passed ) ) ) ;
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
