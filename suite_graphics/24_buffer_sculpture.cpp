#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/array_object.h>
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
#include <motor/std/vector>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <type_traits>

namespace sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using clock_t = std::chrono::steady_clock ;

    constexpr float_t pi = 3.14159265359f ;
    constexpr size_t vertices_per_cube = 24 ;
    constexpr size_t indices_per_cube = 36 ;

    struct options
    {
        size_t count = 20000 ;
        bool_t dual = false ;
        bool_t d3d_only = false ;
        bool_t smoke = false ;
        bool_t cpu_only = false ;
        bool_t still = false ;
        bool_t vsync = true ;
        bool_t ui = true ;
    } ;

    struct mesh_record { vec4_t position ; vec4_t normal ; } ;
    struct object_record
    {
        vec4_t position_scale ;
        vec4_t rotation ;
        vec4_t color ;
    } ;
    static_assert( std::is_standard_layout<object_record>::value, "Buffer records need fixed offsets" ) ;
    static_assert( sizeof( mesh_record ) == 32 && sizeof( object_record ) == 48, "float4 buffer stride" ) ;
    static_assert( offsetof( object_record, rotation ) == 16 &&
        offsetof( object_record, color ) == 32, "Shader fetch offsets" ) ;

    struct seed
    {
        vec3_t sphere ;
        vec3_t spiral ;
        vec3_t wave ;
        vec3_t axis ;
        float_t phase ;
        float_t hue ;
    } ;

    float_t smooth( float_t value ) noexcept
    {
        value = std::max( 0.0f, std::min( 1.0f, value ) ) ;
        return value * value * ( 3.0f - 2.0f * value ) ;
    }

    motor::vector<seed> make_seeds( size_t const count )
    {
        motor::vector<seed> seeds( count ) ;
        size_t const side = size_t( std::ceil( std::sqrt( double_t( count ) ) ) ) ;
        size_t const rings = ( count + 511 ) / 512 ;
        for( size_t i = 0 ; i < count ; ++i )
        {
            auto & s = seeds[i] ;
            float_t const u = ( float_t( i ) + 0.5f ) / float_t( count ) ;
            float_t const angle = std::fmod( float_t( i ) * 2.39996323f, 2.0f * pi ) ;
            float_t const y = 1.0f - 2.0f * u ;
            float_t const radial = std::sqrt( std::max( 0.0f, 1.0f - y * y ) ) ;
            s.sphere = vec3_t( radial * std::cos( angle ), y, radial * std::sin( angle ) ) * 11.0f ;

            float_t const along = ( float_t( i % 512 ) + 0.5f ) / 512.0f ;
            float_t const turn = along * 4.0f * pi ;
            float_t const tube = 1.7f * std::sqrt( ( float_t( i / 512 ) + 0.5f ) / float_t( rings ) ) ;
            float_t const radius = 7.5f + tube * std::cos( angle ) ;
            s.spiral = vec3_t( radius * std::cos( turn ), ( along - 0.5f ) * 22.0f +
                tube * std::sin( angle ), radius * std::sin( turn ) ) ;

            s.wave = vec3_t( ( ( float_t( i % side ) + 0.5f ) / float_t( side ) - 0.5f ) * 26.0f,
                0.0f, ( ( float_t( i / side ) + 0.5f ) / float_t( side ) - 0.5f ) * 26.0f ) ;
            s.axis = vec3_t( std::cos( angle ), 0.8f, std::sin( angle ) ).normalized() ;
            s.phase = angle ;
            s.hue = u ;
        }
        return seeds ;
    }

    struct formation
    {
        size_t first ;
        size_t second ;
        float_t blend ;
    } ;

    formation select_formation( float_t const time, int_t const mode ) noexcept
    {
        if( mode != 0 ) return { size_t( mode - 1 ), size_t( mode - 1 ), 0.0f } ;
        float_t const cycle = std::fmod( time, 24.0f ) ;
        size_t const first = size_t( cycle / 8.0f ) ;
        return { first, ( first + 1 ) % 3, smooth( ( std::fmod( cycle, 8.0f ) - 4.0f ) * 0.25f ) } ;
    }

    vec3_t position_for( seed const & s, size_t const form, float_t const time ) noexcept
    {
        if( form == 0 ) return s.sphere * ( 1.0f + 0.045f * std::sin( time * 0.7f + s.phase ) ) ;
        if( form == 1 ) return s.spiral * ( 1.0f + 0.035f * std::sin( time + s.phase ) ) ;
        return s.wave + vec3_t( 0.0f, 2.3f * std::sin( s.wave.x() * 0.32f + time ) +
            1.8f * std::cos( s.wave.z() * 0.28f - time * 0.8f ), 0.0f ) ;
    }

    object_record evaluate( seed const & s, formation const f, float_t const time,
        float_t const size, int_t const palette ) noexcept
    {
        vec3_t const position = position_for( s, f.first, time ) * ( 1.0f - f.blend ) +
            position_for( s, f.second, time ) * f.blend ;
        float_t const half_angle = 0.5f * ( s.phase + time * ( 0.3f + s.hue * 0.4f ) ) ;
        vec3_t const axis = s.axis * std::sin( half_angle ) ;
        float_t const t = 0.5f + 0.5f * std::sin( s.hue * 2.0f * pi + time * 0.22f ) ;
        vec3_t const a = palette == 0 ? vec3_t( 0.08f, 0.78f, 0.72f ) :
            ( palette == 1 ? vec3_t( 0.12f, 0.42f, 0.96f ) : vec3_t( 0.84f, 0.12f, 0.24f ) ) ;
        vec3_t const b = palette == 0 ? vec3_t( 1.0f, 0.28f, 0.18f ) :
            ( palette == 1 ? vec3_t( 1.0f, 0.75f, 0.12f ) : vec3_t( 0.76f, 0.91f, 0.94f ) ) ;
        vec3_t const color = a * ( 1.0f - t ) + b * t ;
        float_t const scale = size * ( 0.85f + 0.15f * std::sin( time + s.phase ) ) ;
        return { vec4_t( position, scale ), vec4_t( axis, std::cos( half_angle ) ), vec4_t( color, 1.0f ) } ;
    }

    bool_t valid( object_record const & r ) noexcept
    {
        for( auto const & v : { r.position_scale, r.rotation, r.color } )
        {
            for( size_t i = 0 ; i < 4 ; ++i ) if( !std::isfinite( v[i] ) ) return false ;
        }
        float_t const norm = r.rotation.dot( r.rotation ) ;
        return r.position_scale.w() > 0.0f && std::abs( norm - 1.0f ) < 0.0001f &&
            std::abs( r.position_scale.x() ) < 20.0f && std::abs( r.position_scale.y() ) < 20.0f &&
            std::abs( r.position_scale.z() ) < 20.0f && r.color.w() == 1.0f ;
    }

    bool_t cpu_tests( void_t )
    {
        bool_t passed = true ;
        auto const check = [&]( char const * label, bool_t const ok )
        {
            motor::log::global_t::test( motor::string_t( "[buffer sculpture] " ) + label, ok ) ;
            passed = passed && ok ;
        } ;
        for( size_t const count : { size_t( 1 ), size_t( 17 ), size_t( 20000 ), size_t( 100000 ) } )
        {
            auto const seeds = make_seeds( count ) ;
            bool_t records_ok = seeds.size() == count ;
            for( float_t const time : { 0.0f, 6.0f, 8.0f, 14.0f, 16.0f, 22.0f, 24.0f } )
            {
                auto const f = select_formation( time, 0 ) ;
                for( auto const & s : seeds ) records_ok = valid( evaluate( s, f, time, 0.09f, 0 ) ) && records_ok ;
            }
            check( ( "finite transforms and normalized rotations, count=" + motor::to_string( count ) ).c_str(), records_ok ) ;
        }
        auto const seeds = make_seeds( 257 ) ;
        bool_t continuous = true ;
        for( float_t const boundary : { 8.0f, 16.0f, 24.0f } )
        {
            for( auto const & s : seeds )
            {
                auto const a = evaluate( s, select_formation( boundary - 0.001f, 0 ), boundary - 0.001f, 0.1f, 0 ) ;
                auto const b = evaluate( s, select_formation( boundary + 0.001f, 0 ), boundary + 0.001f, 0.1f, 0 ) ;
                for( size_t i = 0 ; i < 3 ; ++i ) continuous = continuous &&
                    std::abs( a.position_scale[i] - b.position_scale[i] ) < 0.02f ;
            }
        }
        check( "morphs remain continuous at formation boundaries", continuous ) ;
        bool_t changes = false, modes_ok = true ;
        for( int_t mode = 1 ; mode <= 3 ; ++mode )
        {
            for( int_t palette = 0 ; palette < 3 ; ++palette )
            {
                auto const a = evaluate( seeds[53], select_formation( 1.0f, mode ), 1.0f, 0.03f, palette ) ;
                auto const b = evaluate( seeds[53], select_formation( 2.0f, mode ), 2.0f, 0.18f, palette ) ;
                modes_ok = modes_ok && valid( a ) && valid( b ) ;
                changes = changes || a.rotation.x() != b.rotation.x() ;
            }
        }
        check( "manual formations, palettes and size limits", modes_ok ) ;
        check( "CPU animation changes the uploaded rotation", changes ) ;
        {
            auto buffer = motor::graphics::data_buffer_t()
                .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
            bool_t stride_ok = false ;
            buffer.resize( seeds.size() ).update<object_record>( [&]( object_record * ptr, size_t const n )
            {
                stride_ok = n == seeds.size() ;
                for( size_t i = 0 ; i < n ; ++i ) ptr[i] = evaluate( seeds[i], select_formation( 0.0f, 0 ), 0.0f, 0.09f, 0 ) ;
                stride_ok = stride_ok && valid( ptr[n-1] ) ;
            } ) ;
            check( "three float4 elements match the 48-byte CPU record", stride_ok ) ;
        }
        return passed ;
    }

    class sculpture_app : public motor::application::app
    {
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            size_t revision = size_t( -1 ) ;
            size_t phase_draws = 0 ;
            size_t phase_updates = 0 ;
            size_t uploads = 0 ;
            size_t upload_bytes = 0 ;
            float_t aspect = 16.0f / 9.0f ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::data_variable<motor::math::mat4f_t> * view = nullptr ;
        } ;

        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::vector<seed> _seeds ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::array_object_t _mesh ;
        motor::graphics::array_object_t _objects ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        motor::graphics::state_object_t _state ;
        size_t _revision = 0 ;
        size_t _phase = 0 ;
        int_t _active ;
        int_t _draw_count = 0 ;
        int_t _formation = 0 ;
        int_t _palette = 0 ;
        bool_t _dirty = true ;
        bool_t _paused = false ;
        bool_t _orbit = true ;
        bool_t _quitting = false ;
        float_t _time = 0.0f ;
        float_t _size = 0.09f ;
        float_t _speed = 1.0f ;
        float_t _yaw = 25.0f ;
        float_t _elevation = 22.0f ;
        float_t _span = 32.0f ;
        double_t _prepare_ms = 0.0 ;
        double_t _frame_ms = 0.0 ;
        clock_t::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }
        size_t object_bytes( void_t ) const noexcept { return _options.count * sizeof( object_record ) ; }
        size_t geometry_bytes( void_t ) const noexcept
        {
            return _options.count * ( vertices_per_cube * sizeof( vec3_t ) + indices_per_cube * sizeof( uint_t ) ) ;
        }

        void_t fail( char const * message ) noexcept
        {
            motor::log::global_t::test( motor::string_t( "[buffer sculpture] " ) + message, false ) ;
            *_passed = false ;
            _quitting = true ;
            close() ;
        }

        void_t update_camera( window_data & window ) noexcept
        {
            float_t const yaw = _yaw * pi / 180.0f ;
            float_t const elevation = _elevation * pi / 180.0f ;
            vec3_t const eye( -40.0f * std::cos( elevation ) * std::sin( yaw ),
                40.0f * std::sin( elevation ), 40.0f * std::cos( elevation ) * std::cos( yaw ) ) ;
            float_t const width = _span * std::max( 1.0f, window.aspect ) ;
            float_t const height = _span / std::min( 1.0f, window.aspect ) ;
            window.camera.make_orthographic( width, height, 0.1f, 100.0f ) ;
            window.camera.look_at( eye, vec3_t( 0.0f, 1.0f, 0.0f ), vec3_t( 0.0f ) ) ;
            window.view->set( window.camera.get_proj_matrix() * window.camera.get_view_matrix() ) ;
        }

        void_t update_objects( void_t ) noexcept
        {
            auto const start = clock_t::now() ;
            auto const form = select_formation( _time, _formation ) ;
            _objects.data_buffer().update<object_record>( [&]( object_record * ptr, size_t const n )
            {
                // The tail stays initialized; the fixed-capacity array is uploaded as a whole.
                size_t const active = std::min( n, size_t( _active ) ) ;
                for( size_t i = 0 ; i < active ; ++i ) ptr[i] = evaluate( _seeds[i], form, _time, _size, _palette ) ;
            } ) ;
            _prepare_ms = std::chrono::duration<double_t, std::milli>( clock_t::now() - start ).count() ;
            _draw_count = _active ;
            ++_revision ;
            _dirty = false ;
        }

        void_t open_window( size_t const index ) noexcept
        {
            bool_t const d3d = index == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | buffer sculpture | d3d11" : "motor | buffer sculpture | gl4" ;
            info.x = int_t( 20 + index * 860 ) ;
            info.y = 60 ;
            info.w = _options.dual ? 840 : 1920 ;
            info.h = _options.dual ? 720 : 1080 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            auto & window = _windows[index] ;
            window.aspect = float_t( info.w ) / float_t( info.h ) ;
            window.id = create_window( info ) ;
            send_window_message( window.id, [vsync = _options.vsync]( auto & target )
            {
                target.send_message( motor::application::show_message( { true } ) ) ;
                target.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
            } ) ;
        }

    public:
        sculpture_app( options const opts, bool_t * passed ) noexcept : _options( opts ),
            _passed( passed ), _active( int_t( opts.count ) ), _paused( opts.still ) {}

#ifdef _MSC_VER
#pragma warning( push )
// Existing motor vertex/index resize helpers divide by their initial zero count.
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = clock_t::now() ;
            _seeds = make_seeds( _options.count ) ;
            {
                auto buffer = motor::graphics::data_buffer_t()
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                buffer.resize( vertices_per_cube ).update<mesh_record>( []( mesh_record * ptr, size_t )
                {
                    float_t const p[8][3] = {
                        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                        {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} } ;
                    uint_t const faces[6][4] = {
                        {4,5,6,7}, {1,0,3,2}, {0,4,7,3}, {5,1,2,6}, {3,7,6,2}, {0,1,5,4} } ;
                    vec4_t const normals[6] = { vec4_t(0,0,1,0), vec4_t(0,0,-1,0),
                        vec4_t(-1,0,0,0), vec4_t(1,0,0,0), vec4_t(0,1,0,0), vec4_t(0,-1,0,0) } ;
                    for( size_t f = 0 ; f < 6 ; ++f )
                    {
                        for( size_t v = 0 ; v < 4 ; ++v )
                        {
                            auto const * point = p[faces[f][v]] ;
                            ptr[f * 4 + v] = { vec4_t( point[0], point[1], point[2], 1.0f ), normals[f] } ;
                        }
                    }
                } ) ;
                _mesh = motor::graphics::array_object_t( "sculpture.mesh", std::move( buffer ) ) ;
            }
            {
                auto buffer = motor::graphics::data_buffer_t()
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                buffer.resize( _options.count ).update<object_record>( [&]( object_record * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = evaluate( _seeds[i], select_formation( 0.0f, 0 ), 0.0f, _size, 0 ) ;
                } ) ;
                _objects = motor::graphics::array_object_t( "sculpture.objects", std::move( buffer ) ) ;
            }
            {
                auto vb = motor::graphics::vertex_buffer_t().add_layout_element(
                    motor::graphics::vertex_attribute::position, motor::graphics::type::tfloat,
                    motor::graphics::type_struct::vec3 ) ;
                vb.resize( _options.count * vertices_per_cube ).update<vec3_t>( []( vec3_t * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = vec3_t( 0.0f ) ;
                } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
                ib.resize( _options.count * indices_per_cube ).update<uint_t>( [&]( uint_t * ptr, size_t )
                {
                    uint_t const pattern[6] = { 2, 0, 1, 0, 2, 3 } ;
                    size_t cursor = 0 ;
                    for( size_t object = 0 ; object < _options.count ; ++object )
                    {
                        for( size_t face = 0 ; face < 6 ; ++face )
                        {
                            uint_t const base = uint_t( object * vertices_per_cube + face * 4 ) ;
                            for( auto const index : pattern ) ptr[cursor++] = base + index ;
                        }
                    }
                } ) ;
                _geometry = motor::graphics::geometry_object_t( "sculpture.geometry",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "sculpture.shader" ) ;
                // Expand quaternion cross products with ' because cross is not translated yet.
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config buffer_sculpture
                    {
                        vertex_shader
                        {
                            uint_t vid : vertex_id ;
                            data_buffer_t u_mesh ;
                            data_buffer_t u_objects ;
                            mat4_t u_view_projection ;
                            out vec4_t pos : position ;
                            out vec3_t normal : texcoord0 ;
                            out vec4_t tint : color ;
                            void main()
                            {
                                int_t object_id = vid / 24 ;
                                int_t vertex_id = vid % 24 ;
                                vec3_t local = fetch_data( u_mesh, vertex_id * 2 ).xyz ;
                                vec3_t normal = fetch_data( u_mesh, vertex_id * 2 + 1 ).xyz ;
                                vec4_t object = fetch_data( u_objects, object_id * 3 ) ;
                                vec4_t rotation = fetch_data( u_objects, object_id * 3 + 1 ) ;
                                vec4_t tint = fetch_data( u_objects, object_id * 3 + 2 ) ;
                                vec3_t a = rotation.yzx ' local.zxy ;
                                vec3_t b = rotation.zxy ' local.yzx ;
                                vec3_t t = ( a - b ) * 2.0 ;
                                vec3_t c = rotation.yzx ' t.zxy ;
                                vec3_t d = rotation.zxy ' t.yzx ;
                                vec3_t rotated = local + t * rotation.w + c - d ;
                                vec3_t na = rotation.yzx ' normal.zxy ;
                                vec3_t nb = rotation.zxy ' normal.yzx ;
                                vec3_t nt = ( na - nb ) * 2.0 ;
                                vec3_t nc = rotation.yzx ' nt.zxy ;
                                vec3_t nd = rotation.zxy ' nt.yzx ;
                                vec3_t n = normal + nt * rotation.w + nc - nd ;
                                vec3_t position = object.xyz + rotated * object.w ;
                                out.pos = u_view_projection * vec4_t( position, 1.0 ) ;
                                out.normal = n ;
                                out.tint = tint ;
                            }
                        }
                        pixel_shader
                        {
                            in vec3_t normal : texcoord0 ;
                            in vec4_t tint : color ;
                            out vec4_t color : color ;
                            void main()
                            {
                                vec3_t n = normalize( in.normal ) ;
                                float_t key = max( dot( n, normalize( vec3_t( -0.6, 1.0, 0.8 ) ) ), 0.0 ) ;
                                float_t fill = max( dot( n, normalize( vec3_t( 0.8, 0.2, -0.5 ) ) ), 0.0 ) ;
                                vec3_t color = in.tint.xyz * ( 0.24 + 0.76 * key ) +
                                    vec3_t( 0.08, 0.12, 0.16 ) * fill ;
                                out.color = vec4_t( color, 1.0 ) ;
                            }
                        }
                    }
                )" ) ;
                shader.link_geometry( "sculpture.geometry" ) ;
                for( size_t i = 0 ; i < window_count() ; ++i )
                {
                    motor::graphics::variable_set_t vars ;
                    vars.array_variable( "u_mesh" )->set( "sculpture.mesh" ) ;
                    vars.array_variable( "u_objects" )->set( "sculpture.objects" ) ;
                    _windows[i].view = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    update_camera( _windows[i] ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _shader = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.depth_s.ss.compare_funk = motor::graphics::depth_compare::less ;
                states.blend_s.do_change = true ;
                states.blend_s.ss.do_activate = false ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = true ;
                states.polygon_s.ss.ff = motor::graphics::front_face::counter_clock_wise ;
                states.polygon_s.ss.cm = motor::graphics::cull_mode::back ;
                states.polygon_s.ss.fm = motor::graphics::fill_mode::fill ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = vec4_t( 0.018f, 0.023f, 0.027f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "sculpture.states" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) { _active = 1 ; _paused = false ; }
            for( size_t i = 0 ; i < window_count() ; ++i ) open_window( i ) ;
            motor::log::global_t::status( "[buffer sculpture] capacity=" + motor::to_string( _options.count ) +
                "; object bytes/upload/backend=" + motor::to_string( object_bytes() ) +
                "; dummy VB + IB bytes/backend=" + motor::to_string( geometry_bytes() ) ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            _frame_ms = _frame_ms * 0.9 + double_t( data.sec_dt ) * 1000.0 * 0.1 ;
            if( !_paused )
            {
                _time += ( _options.smoke ? 0.04f : data.sec_dt ) * _speed ;
                if( _orbit ) _yaw = std::fmod( _yaw + data.sec_dt * 5.0f, 360.0f ) ;
            }
            // on_graphics follows the shared-data synchronization for all windows.
            if( !_paused || _dirty ) update_objects() ;
            else _prepare_ms = 0.0 ;
            for( size_t i = 0 ; i < window_count() ; ++i ) update_camera( _windows[i] ) ;
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->configure<motor::graphics::array_object_t>( &_mesh ) ;
            fe->configure<motor::graphics::array_object_t>( &_objects ) ;
            fe->configure<motor::graphics::msl_object_t>( _shader ) ;
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>( _shader ) ;
            fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->release<motor::graphics::array_object_t>( &_mesh ) ;
            fe->release<motor::graphics::array_object_t>( &_objects ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            size_t const index = id == _windows[0].id ? 0 : 1 ;
            auto & window = _windows[index] ;
            window.upload_bytes = 0 ;
            fe->push( &_state ) ;
            auto const status = fe->decode( _shader ) ;
            if( !_quitting && status.second == motor::graphics::result::failed )
            {
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
                fail( "shader compilation failed" ) ;
                return ;
            }
            if( !_quitting && status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok )
            {
                if( window.revision != _revision )
                {
                    fe->update( &_objects ) ;
                    window.revision = _revision ;
                    window.upload_bytes = object_bytes() ;
                    ++window.phase_updates ;
                    ++window.uploads ;
                }
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.varset = index ;
                detail.num_elems = size_t( _draw_count ) * indices_per_cube ;
                fe->render( _shader, detail ) ;
                ++window.phase_draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting || !_options.ui || _options.smoke ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 44 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 360, 0 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Buffer sculpture", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::Text( "%d cubes | %zu triangles", _draw_count, size_t( _draw_count ) * 12 ) ;
                ImGui::TextUnformatted( "1 scene draw / window | 2 array buffers" ) ;
                ImGui::Text( "Object buffer: %.2f MiB / backend", double_t( object_bytes() ) / 1048576.0 ) ;
                ImGui::Text( "Dummy VB + IB: %.2f MiB / backend", double_t( geometry_bytes() ) / 1048576.0 ) ;
                ImGui::Text( "CPU buffer fill: %.3f ms", _prepare_ms ) ;
                ImGui::Text( "App frame interval (EMA): %.2f ms", _frame_ms ) ;
                for( size_t i = 0 ; i < window_count() ; ++i )
                {
                    auto const & window = _windows[i] ;
                    ImGui::Text( "%s queued: %zu bytes | %zu uploads", ( i == 1 || _options.d3d_only ) ? "d3d11" : "gl4",
                        window.upload_bytes, window.uploads ) ;
                }
                ImGui::Separator() ;
                _dirty |= ImGui::SliderInt( "Cubes", &_active, 1, int_t( _options.count ) ) ;
                _dirty |= ImGui::Combo( "Formation", &_formation, "Cycle\0Sphere\0Spiral\0Waves\0") ;
                _dirty |= ImGui::Combo( "Palette", &_palette, "Coral / turquoise\0Gold / azure\0Crimson / ice\0") ;
                _dirty |= ImGui::SliderFloat( "Cube size", &_size, 0.03f, 0.18f, "%.3f" ) ;
                ImGui::SliderFloat( "Speed", &_speed, 0.1f, 3.0f ) ;
                ImGui::SliderFloat( "Yaw", &_yaw, 0.0f, 360.0f ) ;
                ImGui::SliderFloat( "Elevation", &_elevation, 5.0f, 65.0f ) ;
                ImGui::SliderFloat( "Camera span", &_span, 28.0f, 50.0f ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::SameLine() ;
                ImGui::Checkbox( "Orbit", &_orbit ) ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < window_count() ; ++i ) if( _windows[i].phase_draws < 12 ) return ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                auto const & window = _windows[i] ;
                bool_t const ok = _paused ? window.phase_updates == 1 : window.phase_updates >= 12 ;
                motor::log::global_t::test( "[buffer sculpture] phase=" + motor::to_string( _phase ) +
                    " backend=" + motor::string_t( ( i == 1 || _options.d3d_only ) ? "d3d11" : "gl4" ) +
                    " cubes=" + motor::to_string( _draw_count ) + " draws=" + motor::to_string( window.phase_draws ) +
                    " updates=" + motor::to_string( window.phase_updates ), ok ) ;
                if( !ok ) { fail( "buffer update cadence" ) ; return ; }
            }
            if( ++_phase == 7 )
            {
                *_passed = true ;
                _quitting = true ;
                motor::log::global_t::test( "[buffer sculpture] lifecycle and uploads; pixels require visual verification", true ) ;
                close() ;
                return ;
            }
            _active = _phase == 1 ? int_t( std::max( size_t( 1 ), _options.count / 4 ) ) :
                ( _phase == 4 ? int_t( std::min( size_t( 17 ), _options.count ) ) :
                ( _phase == 5 ? int_t( std::max( size_t( 1 ), _options.count / 2 ) ) : int_t( _options.count ) ) ) ;
            _formation = _phase < 4 ? int_t( _phase ) : 0 ;
            _paused = _phase == 4 || _phase == 5 ;
            _time = float_t( _phase ) * 3.5f ;
            _palette = int_t( _phase % 3 ) ;
            _dirty = true ;
            for( auto & window : _windows ) { window.phase_draws = 0 ; window.phase_updates = 0 ; }
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
            if( state.resize_changed && state.resize_msg.resize && state.resize_msg.w > 0 && state.resize_msg.h > 0 )
                window.aspect = float_t( state.resize_msg.w ) / float_t( state.resize_msg.h ) ;
            if( state.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting && clock_t::now() - _started > std::chrono::seconds( 60 ) )
                fail( "smoke completed before timeout" ) ;
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & window : _windows ) window.view = nullptr ;
            motor::release( motor::move( _shader ) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d_only = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d_only = true ; }
        else if( arg == "--dual" ) { opts.dual = true ; opts.d3d_only = false ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--cpu-only" ) opts.cpu_only = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( arg == "--no-ui" ) opts.ui = false ;
        else if( arg == "--count" && i + 1 < argc )
        {
            char * end = nullptr ;
            auto const n = std::strtoul( argv[++i], &end, 10 ) ;
            if( end == argv[i] || *end != '\0' || n < 1 || n > 100000 )
            {
                motor::log::global_t::error( "--count must be an integer in [1, 100000]" ) ;
                motor::log::global_t::deinit() ;
                return 2 ;
            }
            opts.count = size_t( n ) ;
        }
        else
        {
            motor::log::global_t::status( "Usage: 24_buffer_sculpture [--gl-only|--d3d-only|--dual] [--count 1..100000] [--smoke|--cpu-only] [--still] [--no-vsync] [--no-ui]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( !opts.cpu_only && ( opts.d3d_only || opts.dual ) )
    {
        motor::log::global_t::error( "d3d11 requires Windows." ) ;
        motor::log::global_t::deinit() ;
        return 2 ;
    }
#endif
    bool passed = !opts.smoke && !opts.cpu_only ;
    int result = 0 ;
    if( opts.cpu_only ) passed = sample::cpu_tests() ;
    else
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::sculpture_app( opts, &passed ) ) ) ;
        result = carrier->exec() ;
        motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global::deinit() ;
    motor::log::global::deinit() ;
    motor::profiling::global::deinit() ;
    motor::io::global::deinit() ;
    auto const remaining = motor::memory::global::dump_to_std() ;
    return result != 0 ? result : ( remaining != 0 || !passed ? 1 : 0 ) ;
}
