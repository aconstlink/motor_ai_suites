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

    struct options
    {
        size_t count = 100000 ;
        bool_t dual = false ;
        bool_t d3d_only = false ;
        bool_t smoke = false ;
        bool_t cpu_only = false ;
        bool_t still = false ;
        bool_t ui = true ;
        bool_t vsync = true ;
    } ;

    // The two w components retain stable seeds; xyz is persistent simulation state.
    struct particle { vec4_t position ; vec4_t velocity ; } ;
    static_assert( std::is_standard_layout<particle>::value, "Streamout record layout" ) ;
    static_assert( sizeof( particle ) == 32 && offsetof( particle, velocity ) == 16,
        "Position and velocity must occupy two consecutive float4s" ) ;

    particle make_particle( size_t const i, size_t const count ) noexcept
    {
        float_t const u = ( float_t( i ) + 0.5f ) / float_t( count ) ;
        float_t const v = float_t( std::fmod( double_t( i ) * 0.6180339887498948, 1.0 ) ) ;
        float_t const y = 1.0f - 2.0f * u ;
        float_t const r = std::sqrt( std::max( 0.0f, 1.0f - y * y ) ) ;
        return { vec4_t( r * std::cos( v * 2.0f * pi ) * 10.5f, y * 10.5f,
            r * std::sin( v * 2.0f * pi ) * 10.5f, u ), vec4_t( 0.0f, 0.0f, 0.0f, v ) } ;
    }

    motor::graphics::vertex_buffer_t make_layout( void_t )
    {
        return motor::graphics::vertex_buffer_t()
            .add_layout_element( motor::graphics::vertex_attribute::position,
                motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
            .add_layout_element( motor::graphics::vertex_attribute::color0,
                motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
    }

    float_t bounded_dt( float_t const dt ) noexcept
    {
        return std::max( 0.0f, std::min( 1.0f / 30.0f, dt ) ) ;
    }

    bool_t cpu_tests( void_t )
    {
        bool_t passed = true ;
        auto check = [&]( char const * message, bool_t const ok )
        {
            motor::log::global_t::test( motor::string_t( "[gpu sculpture] " ) + message, ok ) ;
            passed = passed && ok ;
        } ;
        for( size_t const count : { size_t( 1 ), size_t( 17 ), size_t( 100000 ), size_t( 500000 ) } )
        {
            bool_t ok = true ;
            for( size_t i = 0 ; i < count ; ++i )
            {
                auto const p = make_particle( i, count ) ;
                float_t const radius = std::sqrt( p.position.x()*p.position.x() +
                    p.position.y()*p.position.y() + p.position.z()*p.position.z() ) ;
                ok = ok && std::isfinite( radius ) && std::fabs( radius - 10.5f ) < 0.0001f &&
                    p.position.w() > 0.0f && p.position.w() < 1.0f &&
                    p.velocity.x() == 0.0f && p.velocity.y() == 0.0f && p.velocity.z() == 0.0f &&
                    p.velocity.w() >= 0.0f && p.velocity.w() < 1.0f ;
            }
            check( ( "finite sphere seeds, capacity=" + motor::to_string( count ) ).c_str(), ok ) ;
        }
        check( "time step clamps stalls and negative deltas", bounded_dt( -1.0f ) == 0.0f &&
            bounded_dt( 2.0f ) == 1.0f / 30.0f && bounded_dt( 0.01f ) == 0.01f ) ;
        {
            auto vb = make_layout() ;
            bool_t layout_ok = true ;
            vb.resize( 17 ).update<particle>( [&]( particle * ptr, size_t const n )
            {
                layout_ok = n == 17 ;
                for( size_t i = 0 ; i < n ; ++i ) ptr[i] = make_particle( i, n ) ;
                layout_ok = layout_ok && reinterpret_cast<char const *>( &ptr[1] ) -
                    reinterpret_cast<char const *>( &ptr[0] ) == 32 ;
            } ) ;
            check( "seed vertex buffer has 32-byte records", layout_ok ) ;
        }
        // Numerical reference for the shader's four semi-implicit spring substeps.
        // This is not GPU readback and cannot establish shader/output equivalence.
        bool_t stable = true ;
        for( float_t const dt : { 1.0f / 240.0f, 1.0f / 60.0f, 1.0f / 30.0f } )
        for( float_t const spring : { 6.0f, 24.0f, 60.0f } )
        for( float_t const damping : { 1.0f, 3.6f, 12.0f } )
        {
            float_t position = 30.0f, velocity = 0.0f ;
            size_t const frames = size_t( std::ceil( 20.0f / dt ) ) ;
            for( size_t frame = 0 ; frame < frames ; ++frame )
            for( size_t substep = 0 ; substep < 4 ; ++substep )
            {
                velocity += ( -position * spring - velocity * damping ) * ( dt * 0.25f ) ;
                position += velocity * ( dt * 0.25f ) ;
                stable = stable && std::isfinite( position ) && std::isfinite( velocity ) &&
                    std::fabs( position ) <= 31.0f ;
            }
            stable = stable && std::fabs( position ) < 0.02f && std::fabs( velocity ) < 0.02f ;
            float_t const before = position ;
            velocity += ( -position * spring - velocity * damping ) * 0.0f ;
            position += velocity * 0.0f ;
            stable = stable && position == before ;
        }
        check( "spring reference stays finite, converges and preserves a zero step", stable ) ;
        return passed ;
    }

    class sculpture_app : public motor::application::app
    {
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            motor::gfx::generic_camera_t camera ;
            float_t aspect = 16.0f / 9.0f ;
            motor::graphics::data_variable<vec4_t> * step = nullptr ;
            motor::graphics::data_variable<vec4_t> * shape = nullptr ;
            motor::graphics::data_variable<vec4_t> * appearance = nullptr ;
            motor::graphics::data_variable<motor::math::mat4f_t> * view = nullptr ;
            bool_t initialized = false ;
            size_t reset_seen = 0 ;
            size_t frames = 0 ;
            size_t updates = 0 ;
            size_t seeds = 0 ;
            size_t feedbacks = 0 ;
            size_t draws_this_frame = 0 ;
        } ;
        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _seed ;
        motor::graphics::streamout_object_t _state_buffer ;
        motor::graphics::msl_object_mtr_t _simulate = nullptr ;
        motor::graphics::msl_object_mtr_t _display = nullptr ;
        motor::graphics::state_object_t _state ;
        size_t _phase = 0 ;
        size_t _reset = 0, _frame_reset = 0 ;
        float_t _time = 0.0f ;
        float_t _spring = 24.0f, _damping = 3.6f, _swirl = 0.35f ;
        float_t _size = 0.045f, _speed = 1.0f ;
        float_t _yaw = 32.0f, _elevation = 22.0f, _span = 32.0f ;
        float_t _frame_dt = 0.0f ;
        double_t _frame_ms = 0.0, _prepare_ms = 0.0 ;
        int_t _formation = 0, _palette = 0 ;
        bool_t _paused = false, _frame_paused = false, _orbit = true, _quitting = false ;
        clock_t::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }
        char const * backend_name( size_t const w ) const noexcept
        { return w == 1 || _options.d3d_only ? "d3d11" : "gl4" ; }

        void_t fail( char const * message ) noexcept
        {
            motor::log::global_t::test( motor::string_t( "[gpu sculpture] " ) + message, false ) ;
            *_passed = false ; _quitting = true ; close() ;
        }

        void_t open_window( size_t const w ) noexcept
        {
            motor::application::window_info_t info ;
            info.window_name = motor::string_t( "motor | GPU sculpture | " ) + backend_name( w ) ;
            info.x = int_t( 20 + w * 860 ) ; info.y = 50 ;
            info.w = _options.dual ? 840 : 1920 ; info.h = _options.dual ? 720 : 1080 ;
            info.gen = w == 1 || _options.d3d_only ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[w].aspect = float_t( info.w ) / float_t( info.h ) ;
            _windows[w].id = create_window( info ) ;
            send_window_message( _windows[w].id, [vsync = _options.vsync]( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
            } ) ;
        }

    public:
        sculpture_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ), _paused( opts.still ) {}

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = clock_t::now() ;
            {
                auto vb = make_layout() ;
                // This is the only per-element CPU loop used by the graphical application.
                vb.resize( _options.count ).update<particle>( []( particle * ptr, size_t const n )
                { for( size_t i = 0 ; i < n ; ++i ) ptr[i] = make_particle( i, n ) ; } ) ;
                _seed = motor::graphics::geometry_object_t( "gpu_sculpture_seed",
                    motor::graphics::primitive_type::points, std::move( vb ) ) ;
                _state_buffer = motor::graphics::streamout_object_t( "gpu_sculpture_state", make_layout() ) ;
                _state_buffer.resize( _options.count ) ;
            }
            {
                motor::graphics::msl_object_t shader( "gpu_sculpture_simulate" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config gpu_sculpture_simulate
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t vel : color ;
                            vec4_t u_step ;
                            vec4_t u_shape ;
                            void main()
                            {
                                float_t u = in.pos.w ;
                                float_t a = in.vel.w * 6.28318530718 ;
                                float_t y = 1.0 - 2.0 * u ;
                                float_t r = sqrt( max( 0.0, 1.0 - y * y ) ) ;
                                vec3_t sphere = vec3_t( r * cos( a ), y, r * sin( a ) ) * 10.5 ;
                                float_t turn = u * 12.56637061436 + u_step.x * 0.18 ;
                                float_t radius = 7.0 + 1.4 * cos( a ) ;
                                vec3_t spiral = vec3_t( radius * cos( turn ),
                                    ( u - 0.5 ) * 21.0 + 1.4 * sin( a ), radius * sin( turn ) ) ;
                                float_t x = ( u - 0.5 ) * 26.0 ;
                                float_t z = ( in.vel.w - 0.5 ) * 26.0 ;
                                vec3_t wave = vec3_t( x, 2.3 * sin( x * 0.32 + u_step.x ) +
                                    1.4 * cos( z * 0.42 - u_step.x * 0.7 ), z ) ;
                                float_t cycle = fract( u_step.x / 24.0 ) * 24.0 ;
                                float_t local = cycle - floor( cycle / 8.0 ) * 8.0 ;
                                float_t blend = clamp( ( local - 4.0 ) * 0.25, 0.0, 1.0 ) ;
                                blend = blend * blend * ( 3.0 - 2.0 * blend ) ;
                                vec3_t target = sphere * ( 1.0 - blend ) + spiral * blend ;
                                if( cycle >= 8.0 ) { target = spiral * ( 1.0 - blend ) + wave * blend ; }
                                if( cycle >= 16.0 ) { target = wave * ( 1.0 - blend ) + sphere * blend ; }
                                if( u_shape.x > 0.5 && u_shape.x < 1.5 ) { target = sphere ; }
                                if( u_shape.x > 1.5 && u_shape.x < 2.5 ) { target = spiral ; }
                                if( u_shape.x > 2.5 ) { target = wave ; }
                                vec3_t p = in.pos.xyz ;
                                vec3_t v = in.vel.xyz ;
                                float_t h = u_step.y * 0.25 ;
                                for( int i = 0 ; i < 4 ; ++i )
                                {
                                    vec3_t curl = vec3_t( -p.z, sin( a + u_step.x ), p.x ) * u_shape.y ;
                                    vec3_t acceleration = ( target - p ) * u_step.z - v * u_step.w + curl ;
                                    v = v + acceleration * h ;
                                    p = p + v * h ;
                                }
                                out.pos = vec4_t( p, u ) ;
                                out.vel = vec4_t( v, in.vel.w ) ;
                            }
                        }
                    }
                )" ) ;
                // One object owns the backend's internal ping-pong pair.
                shader.link_geometry( "gpu_sculpture_seed", "gpu_sculpture_state" ) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    motor::graphics::variable_set_t vars ;
                    _windows[w].step = vars.data_variable<vec4_t>( "u_step" ) ;
                    _windows[w].shape = vars.data_variable<vec4_t>( "u_shape" ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _simulate = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "gpu_sculpture_display" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config gpu_sculpture_display
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            vec4_t u_appearance ;
                            void main()
                            {
                                float_t t = 0.5 + 0.5 * sin( in.pos.w * 6.28318530718 + in.color.w * 2.0 ) ;
                                vec3_t first = vec3_t( 0.95, 0.12, 0.065 ) ;
                                vec3_t second = vec3_t( 0.035, 0.72, 0.56 ) ;
                                if( u_appearance.y > 0.5 )
                                { first = vec3_t( 1.0, 0.56, 0.04 ) ; second = vec3_t( 0.045, 0.25, 0.95 ) ; }
                                if( u_appearance.y > 1.5 )
                                { first = vec3_t( 0.7, 0.025, 0.12 ) ; second = vec3_t( 0.55, 0.9, 0.97 ) ; }
                                float_t glow = min( length( in.color.xyz ) * 0.022, 0.35 ) ;
                                vec3_t c = first * ( 1.0 - t ) + second * t ;
                                out.color = vec4_t( c * ( 1.0 - glow ) + as_vec3( glow ), 1.0 ) ;
                                out.pos = in.pos ;
                            }
                        }
                        geometry_shader
                        {
                            in points ;
                            out triangles[ max_verts = 24 ] ;
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            mat4_t u_view_projection ;
                            vec4_t u_appearance ;
                            void main()
                            {
                                for( int i = 0 ; i < in.length() ; ++i )
                                {
                                    float_t size = u_appearance.x * ( 0.8 + 0.4 * in[i].pos.w ) ;
                                    for( int face = 0 ; face < 6 ; ++face )
                                    {
                                        vec3_t n = vec3_t( 0.0, 0.0, 1.0 ) ;
                                        vec3_t u = vec3_t( 1.0, 0.0, 0.0 ) ;
                                        vec3_t v = vec3_t( 0.0, 1.0, 0.0 ) ;
                                        if( face == 1 ) { n = vec3_t( 0.0, 0.0, -1.0 ) ; u = vec3_t( -1.0, 0.0, 0.0 ) ; }
                                        if( face == 2 ) { n = vec3_t( 1.0, 0.0, 0.0 ) ; u = vec3_t( 0.0, 0.0, -1.0 ) ; }
                                        if( face == 3 ) { n = vec3_t( -1.0, 0.0, 0.0 ) ; u = vec3_t( 0.0, 0.0, 1.0 ) ; }
                                        if( face == 4 ) { n = vec3_t( 0.0, 1.0, 0.0 ) ; v = vec3_t( 0.0, 0.0, -1.0 ) ; }
                                        if( face == 5 ) { n = vec3_t( 0.0, -1.0, 0.0 ) ; v = vec3_t( 0.0, 0.0, 1.0 ) ; }
                                        float_t light = 0.32 + 0.68 * max( dot( n, normalize( vec3_t( -0.4, 0.8, 0.6 ) ) ), 0.0 ) ;
                                        vec3_t center = in[i].pos.xyz + n * size ;
                                        out.pos = u_view_projection * vec4_t( center + ( -u - v ) * size, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( u - v ) * size, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( -u + v ) * size, 1.0 ) ;
                                        out.color = vec4_t( in[i].color.xyz * light, 1.0 ) ; emit_vertex() ;
                                        out.pos = u_view_projection * vec4_t( center + ( u + v ) * size, 1.0 ) ;
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
                shader.link_geometry( "gpu_sculpture_seed", "gpu_sculpture_state" ) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    motor::graphics::variable_set_t vars ;
                    _windows[w].view = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    _windows[w].appearance = vars.data_variable<vec4_t>( "u_appearance" ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _display = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.blend_s.do_change = true ;
                states.blend_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = vec4_t( 0.022f, 0.026f, 0.03f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "gpu_sculpture_state_settings" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) { _paused = false ; _orbit = false ; }
            for( size_t w = 0 ; w < window_count() ; ++w ) open_window( w ) ;
            motor::log::global_t::status( "[gpu sculpture] elements=" + motor::to_string( _options.count ) +
                "; seed bytes=" + motor::to_string( _options.count * sizeof( particle ) ) +
                "; ping-pong bytes/backend=" + motor::to_string( _options.count * sizeof( particle ) * 2 ) +
                "; no recurring particle uploads or CPU per-particle updates" ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            auto const begin = clock_t::now() ;
            _frame_ms = _frame_ms * 0.9 + double_t( data.sec_dt ) * 1000.0 * 0.1 ;
            _frame_paused = _paused ;
            if( _frame_reset != _reset ) _time = 0.0f ;
            _frame_reset = _reset ;
            _frame_dt = _frame_paused ? 0.0f : bounded_dt( ( _options.smoke ? 1.0f / 60.0f : data.sec_dt ) * _speed ) ;
            _time += _frame_dt ;
            if( _orbit && !_frame_paused ) _yaw = std::fmod( _yaw + data.sec_dt * 5.0f, 360.0f ) ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto & window = _windows[w] ;
                window.step->set( vec4_t( _time, _frame_dt, _spring, _damping ) ) ;
                window.shape->set( vec4_t( float_t( _formation ), _swirl, 0.0f, 0.0f ) ) ;
                window.appearance->set( vec4_t( _size, float_t( _palette ), 0.0f, 0.0f ) ) ;
                float_t const yaw = _yaw * pi / 180.0f, elevation = _elevation * pi / 180.0f ;
                vec3_t const eye( 42.0f * std::cos( elevation ) * std::sin( yaw ),
                    42.0f * std::sin( elevation ), 42.0f * std::cos( elevation ) * std::cos( yaw ) ) ;
                window.camera.make_orthographic( _span * std::max( 1.0f, window.aspect ),
                    _span / std::min( 1.0f, window.aspect ), 0.1f, 90.0f ) ;
                window.camera.look_at( eye, vec3_t( 0.0f, 1.0f, 0.0f ), vec3_t( 0.0f ) ) ;
                window.view->set( window.camera.get_proj_matrix() * window.camera.get_view_matrix() ) ;
            }
            _prepare_ms = std::chrono::duration<double_t, std::milli>( clock_t::now() - begin ).count() ;
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_seed ) ;
            fe->configure<motor::graphics::streamout_object_t>( &_state_buffer ) ;
            fe->configure<motor::graphics::msl_object_t>( _simulate ) ;
            fe->configure<motor::graphics::msl_object_t>( _display ) ;
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>( _display ) ;
            fe->release<motor::graphics::msl_object_t>( _simulate ) ;
            fe->release<motor::graphics::streamout_object_t>( &_state_buffer ) ;
            fe->release<motor::graphics::geometry_object_t>( &_seed ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            size_t const w = id == _windows[0].id ? 0 : 1 ;
            auto & window = _windows[w] ;
            window.draws_this_frame = 0 ;
            fe->push( &_state ) ;
            bool_t ready = !_quitting ;
            for( auto shader : { _simulate, _display } )
            {
                auto const status = fe->decode( shader ) ;
                if( !_quitting && status.second == motor::graphics::result::failed ) fail( "shader compilation failed" ) ;
                ready = ready && status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok ;
            }
            if( ready && !_quitting )
            {
                bool_t const seed = !window.initialized || window.reset_seen != _frame_reset ;
                if( seed || !_frame_paused )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = w ; detail.num_elems = _options.count ;
                    detail.feed_from_streamout = !seed ;
                    detail.use_streamout_count = !seed ;
                    fe->use( &_state_buffer ) ;
                    fe->render( _simulate, detail ) ;
                    fe->unuse( motor::graphics::gen4::backend::unuse_type::streamout ) ;
                    window.initialized = true ; window.reset_seen = _frame_reset ;
                    ++window.updates ; ++window.draws_this_frame ;
                    if( seed ) ++window.seeds ; else ++window.feedbacks ;
                }
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.varset = w ;
                    detail.feed_from_streamout = true ; detail.use_streamout_count = true ;
                    fe->render( _display, detail ) ;
                    ++window.draws_this_frame ; ++window.frames ;
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || !_options.ui || _options.smoke || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 44 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 370, 0 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "GPU sculpture", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::Text( "%zu elements | %zu triangles", _options.count, _options.count * 12 ) ;
                ImGui::Text( "GPU state pair: %.2f MiB / backend", double_t( _options.count * sizeof( particle ) * 2 ) / 1048576.0 ) ;
                ImGui::TextUnformatted( "Recurring particle uploads: 0 bytes" ) ;
                ImGui::Text( "CPU camera / uniforms: %.3f ms", _prepare_ms ) ;
                ImGui::Text( "App frame interval (EMA): %.2f ms", _frame_ms ) ;
                ImGui::Text( "Simulation step: %.2f ms", _frame_dt * 1000.0f ) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                    ImGui::Text( "%s: %zu draws | %zu simulation passes", backend_name( w ),
                        _windows[w].draws_this_frame, _windows[w].updates ) ;
                ImGui::Separator() ;
                ImGui::Combo( "Formation", &_formation, "Cycle\0Sphere\0Spiral\0Waves\0" ) ;
                ImGui::Combo( "Palette", &_palette, "Coral / turquoise\0Gold / azure\0Crimson / ice\0" ) ;
                ImGui::SliderFloat( "Spring", &_spring, 6.0f, 60.0f ) ;
                ImGui::SliderFloat( "Damping", &_damping, 1.0f, 12.0f ) ;
                ImGui::SliderFloat( "Swirl", &_swirl, 0.0f, 2.0f ) ;
                ImGui::SliderFloat( "Cube size", &_size, 0.02f, 0.14f, "%.3f" ) ;
                ImGui::SliderFloat( "Speed", &_speed, 0.1f, 2.0f ) ;
                ImGui::SliderFloat( "Yaw", &_yaw, 0.0f, 360.0f ) ;
                ImGui::SliderFloat( "Elevation", &_elevation, 5.0f, 65.0f ) ;
                ImGui::SliderFloat( "Camera span", &_span, 28.0f, 55.0f ) ;
                ImGui::Checkbox( "Pause", &_paused ) ; ImGui::SameLine() ; ImGui::Checkbox( "Orbit", &_orbit ) ;
                if( ImGui::Button( "Reset simulation" ) ) ++_reset ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w ) if( _windows[w].frames < 24 ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto const & window = _windows[w] ;
                size_t const expected_seeds = _phase == 0 || _phase == 4 ? 1 : 0 ;
                bool_t const ok = window.seeds == expected_seeds && ( _frame_paused ?
                    window.updates == expected_seeds && window.feedbacks == 0 :
                    window.updates == window.frames && window.feedbacks == window.frames - expected_seeds ) ;
                motor::log::global_t::test( "[gpu sculpture] phase=" + motor::to_string( _phase ) +
                    " backend=" + backend_name( w ) + " frames=" + motor::to_string( window.frames ) +
                    " seed=" + motor::to_string( window.seeds ) + " feedback=" + motor::to_string( window.feedbacks ), ok ) ;
                if( !ok ) { fail( "seed / feedback / pause cadence" ) ; return ; }
            }
            if( ++_phase == 7 )
            {
                *_passed = true ; _quitting = true ;
                motor::log::global_t::test( "[gpu sculpture] submission lifecycle; GPU state needs visual verification", true ) ;
                close() ; return ;
            }
            _paused = _phase == 3 || _phase == 4 ;
            _formation = _phase == 1 ? 2 : ( _phase == 2 ? 3 : ( _phase == 5 ? 1 : 0 ) ) ;
            _palette = int_t( _phase % 3 ) ;
            if( _phase == 4 ) ++_reset ;
            for( auto & window : _windows )
            { window.frames = 0 ; window.updates = 0 ; window.seeds = 0 ; window.feedbacks = 0 ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting && clock_t::now() - _started > std::chrono::seconds( 60 ) )
                fail( "smoke completed before timeout" ) ;
        }
        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.resize_changed && state.resize_msg.resize && state.resize_msg.w > 0 && state.resize_msg.h > 0 )
                _windows[id == _windows[0].id ? 0 : 1].aspect = float_t( state.resize_msg.w ) / float_t( state.resize_msg.h ) ;
            if( state.close_changed ) { _quitting = true ; close() ; }
        }
        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & window : _windows )
            { window.step = nullptr ; window.shape = nullptr ; window.appearance = nullptr ; window.view = nullptr ; }
            motor::release( motor::move( _display ) ) ;
            motor::release( motor::move( _simulate ) ) ;
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
        else if( arg == "--no-ui" ) opts.ui = false ;
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( arg == "--count" && i + 1 < argc )
        {
            char * end = nullptr ;
            auto const n = std::strtoul( argv[++i], &end, 10 ) ;
            if( end == argv[i] || *end != '\0' || n < 1 || n > 500000 )
            {
                motor::log::global_t::error( "--count must be an integer in [1, 500000]" ) ;
                motor::log::global_t::deinit() ; return 2 ;
            }
            opts.count = size_t( n ) ;
        }
        else
        {
            motor::log::global_t::status( "Usage: 25_gpu_sculpture [--gl-only|--d3d-only|--dual] [--count 1..500000] [--smoke|--cpu-only] [--still] [--no-ui] [--no-vsync]" ) ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( !opts.cpu_only && ( opts.d3d_only || opts.dual ) )
    {
        motor::log::global_t::error( "d3d11 requires Windows." ) ;
        motor::log::global_t::deinit() ; return 2 ;
    }
#endif
    bool passed = !opts.smoke && !opts.cpu_only ;
    int result = 0 ;
    if( opts.cpu_only ) passed = sample::cpu_tests() ;
    else
    {
        auto carrier = motor::platform::global_t::create_carrier( motor::shared( sample::sculpture_app( opts, &passed ) ) ) ;
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
