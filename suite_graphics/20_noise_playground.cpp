#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
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
#include <algorithm>

namespace sample
{
    using namespace motor::core::types ;

    struct options
    {
        bool_t d3d = false ;
        bool_t smoke = false ;
        bool_t still = false ;
        bool_t compare = false ;
        bool_t demo = false ;
    } ;

    static constexpr size_t noise_count = 8 ;
    static char const * const names[noise_count] = {
        "Value / noise_1d(vec2)", "perlin_1d(vec2)", "perlin_2d", "perlin_3d",
        "snoise_2d", "snoise_3d", "iqnoise", "fbm_1d(vec3)" } ;

    // Expressions deliberately call the built-ins, rather than reimplementing noise.
    static motor::string_t noise_expression( size_t const kind, motor::string_t const & p )
    {
        switch( kind )
        {
        case 0: return "noise_1d( " + p + ".xy )" ;
        case 1: return "perlin_1d( " + p + ".xy * 8.0, u_octaves )" ;
        case 2: return "( 0.5 + 0.5 * perlin_2d( " + p + ".xy ) )" ;
        case 3: return "( 0.5 + 0.5 * perlin_3d( " + p + " ) )" ;
        case 4: return "( 0.5 + 0.5 * snoise_2d( " + p + ".xy ) )" ;
        case 5: return "( 0.5 + 0.5 * snoise_3d( " + p + " ) )" ;
        case 6: return "iqnoise( " + p + ".xy, u_shape.y, u_shape.z )" ;
        default: return "fbm_1d( " + p + ", u_octaves )" ;
        }
    }

    static motor::string_t shader_source( size_t const kind )
    {
        motor::string_t source = "config noise_playground_" + motor::to_string( kind ) + R"(
        {
            vertex_shader
            {
                in vec3_t pos : position ;
                out vec4_t pos : position ;
                out vec2_t uv : texcoord0 ;
                out vec3_t normal : texcoord1 ;
                out float_t height : texcoord2 ;
                mat4_t u_view_projection ;
                vec4_t u_params ;
                vec4_t u_shape ;
                vec4_t u_rect ;
                int_t u_octaves ;
                int_t u_mode ;
                void main()
                {
                    out.uv = in.pos.xy * 0.5 + vec2_t( 0.5, 0.5 ) ;
                    out.normal = vec3_t( 0.0, 1.0, 0.0 ) ;
                    out.height = 0.0 ;
                    out.pos = vec4_t( in.pos.xy * u_rect.zw + u_rect.xy, 0.5, 1.0 ) ;
                    if( u_mode == 0 )
                    {
                        vec3_t p = vec3_t( in.pos.x * u_params.y + u_params.x * 0.12,
                            in.pos.y * u_params.y + u_params.x * 0.08, u_params.x * 0.22 ) ;
                        float_t h = )" + noise_expression( kind, "p" ) + R"( ;
                        vec3_t px = p + vec3_t( 0.015, 0.0, 0.0 ) ;
                        vec3_t pz = p + vec3_t( 0.0, 0.015, 0.0 ) ;
                        float_t hx = )" + noise_expression( kind, "px" ) + R"( ;
                        float_t hz = )" + noise_expression( kind, "pz" ) + R"( ;
                        float_t derivative_scale = u_params.z * u_params.y / ( 0.015 * 10.0 ) ;
                        out.normal = normalize( vec3_t( -( hx - h ) * derivative_scale,
                            1.0, -( hz - h ) * derivative_scale ) ) ;
                        out.height = h ;
                        vec3_t world = vec3_t( in.pos.x * 10.0, ( h - 0.5 ) * u_params.z, in.pos.y * 10.0 ) ;
                        out.pos = u_view_projection * vec4_t( world, 1.0 ) ;
                    }
                }
            }
            pixel_shader
            {
                in vec2_t uv : texcoord0 ;
                in vec3_t normal : texcoord1 ;
                in float_t height : texcoord2 ;
                out vec4_t color : color ;
                vec4_t u_params ;
                vec4_t u_shape ;
                int_t u_octaves ;
                int_t u_mode ;
                void main()
                {
                    float_t value = in.height ;
                    if( u_mode != 0 )
                    {
                        vec3_t p = vec3_t( ( in.uv.x - 0.5 ) * 2.0 * u_params.y * u_shape.x + u_params.x * 0.12,
                            ( in.uv.y - 0.5 ) * 2.0 * u_params.y + u_params.x * 0.08, u_params.x * 0.22 ) ;
                        value = )" + noise_expression( kind, "p" ) + R"( ;
                    }
                    float_t h = clamp( value, 0.0, 1.0 ) ;
                    vec3_t color = mix( vec3_t( 0.025, 0.12, 0.16 ), vec3_t( 0.08, 0.52, 0.43 ), smoothstep( 0.08, 0.48, h ) ) ;
                    color = mix( color, vec3_t( 0.82, 0.67, 0.42 ), smoothstep( 0.42, 0.70, h ) ) ;
                    color = mix( color, vec3_t( 0.94, 0.97, 0.98 ), smoothstep( 0.68, 0.91, h ) ) ;
                    if( u_params.w > 0.5 ) color = vec3_t( h, h, h ) ;
                    if( u_mode == 0 )
                    {
                        vec3_t n = normalize( in.normal ) ;
                        float_t slope = 1.0 - clamp( n.y, 0.0, 1.0 ) ;
                        if( u_params.w < 0.5 ) color = mix( color, vec3_t( 0.34, 0.38, 0.42 ), smoothstep( 0.25, 0.75, slope ) * 0.65 ) ;
                        float_t key = max( dot( n, normalize( vec3_t( -0.5, 0.9, 0.4 ) ) ), 0.0 ) ;
                        float_t fill = max( dot( n, normalize( vec3_t( 0.8, 0.3, -0.5 ) ) ), 0.0 ) ;
                        color = color * ( 0.28 + 0.72 * key ) + vec3_t( 0.06, 0.09, 0.12 ) * fill ;
                    }
                    out.color = vec4_t( color, 1.0 ) ;
                }
            }
        }
        )" ;
        return source ;
    }

    class noise_app : public motor::application::app
    {
        struct bindings
        {
            motor::graphics::data_variable<motor::math::mat4f_t> * matrix = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * params = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * shape = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * rect = nullptr ;
            motor::graphics::data_variable<int_t> * octaves = nullptr ;
            motor::graphics::data_variable<int_t> * mode = nullptr ;
        } ;
        struct tile { float_t x, y, w, h ; } ;

        options _options ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _grid, _quad ;
        motor::graphics::state_object_t _state ;
        motor::graphics::msl_object_mtr_t _shaders[noise_count] = {} ;
        bindings _bindings[noise_count] ;
        motor::gfx::generic_camera_t _camera ;
        float_t _width = 1920.0f, _height = 1080.0f ;
        float_t _time = 0.0f, _frequency = 3.0f, _amplitude = 5.0f, _speed = 0.6f ;
        float_t _yaw = 35.0f, _jitter = 1.0f, _smoothness = 0.75f ;
        int_t _selected = 5, _mode = 0, _octaves = 5 ;
        int_t _frame_selected = 5, _frame_mode = 0 ;
        bool_t _paused = false, _orbit = true, _gray = false, _quitting = false ;
        bool_t _demo = false, _ready = false ;
        double _demo_time = 0.0 ;
        size_t _demo_step = 0, _frame_demo_step = 0 ;
        uint_t _demo_seen = 0 ;
        size_t _phase = 0, _phase_draws = 0 ;
        std::chrono::steady_clock::time_point _started ;

        tile tile_bounds( size_t const i ) const noexcept
        {
            float_t const left = _width > 1000.0f ? 340.0f : 12.0f ;
            float_t const top = _width > 1000.0f ? 70.0f : 400.0f ;
            float_t const w = ( _width - left - 24.0f - 3.0f * 14.0f ) / 4.0f ;
            float_t const h = std::max( 30.0f, ( _height - top - 30.0f ) / 2.0f - 34.0f ) ;
            return { left + float_t( i % 4 ) * ( w + 14.0f ),
                top + float_t( i / 4 ) * ( h + 34.0f ), w, h } ;
        }

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        static motor::graphics::geometry_object_t make_grid( motor::string_t const & name, size_t const cells )
        {
            size_t const edge = cells + 1 ;
            auto vb = motor::graphics::vertex_buffer_t().add_layout_element(
                motor::graphics::vertex_attribute::position, motor::graphics::type::tfloat,
                motor::graphics::type_struct::vec3 ) ;
            vb.resize( edge * edge ).update<motor::math::vec3f_t>( [&]( auto * ptr, size_t )
            {
                for( size_t y = 0 ; y < edge ; ++y )
                    for( size_t x = 0 ; x < edge ; ++x )
                        ptr[y * edge + x] = motor::math::vec3f_t(
                            2.0f * float_t( x ) / float_t( cells ) - 1.0f,
                            2.0f * float_t( y ) / float_t( cells ) - 1.0f, 0.0f ) ;
            } ) ;
            auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
            ib.resize( cells * cells * 6 ).update<uint_t>( [&]( auto * ptr, size_t )
            {
                size_t cursor = 0 ;
                for( size_t y = 0 ; y < cells ; ++y )
                    for( size_t x = 0 ; x < cells ; ++x )
                    {
                        uint_t const a = uint_t( y * edge + x ), b = a + uint_t( edge ) ;
                        ptr[cursor++] = a ; ptr[cursor++] = b ; ptr[cursor++] = a + 1 ;
                        ptr[cursor++] = a + 1 ; ptr[cursor++] = b ; ptr[cursor++] = b + 1 ;
                    }
            } ) ;
            return motor::graphics::geometry_object_t( name, motor::graphics::primitive_type::triangles,
                std::move( vb ), std::move( ib ) ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t animate_demo( void_t ) noexcept
        {
            // Wall-time cadence is independent of the noise-animation speed.
            _demo_step = size_t( std::fmod( _demo_time, double( noise_count + 1 ) ) ) ;
            _mode = _demo_step == noise_count ? 1 : 0 ;
            _selected = int_t( _demo_step % noise_count ) ;
            float_t const t = float_t( std::fmod( _demo_time, 3600.0 ) ) ;
            _frequency = 2.8f + 1.6f * std::sin( t * 0.65f ) ;
            _amplitude = 5.0f + 2.5f * std::sin( t * 0.83f + 0.4f ) ;
            _speed = 0.75f + 0.35f * std::sin( t * 0.43f ) ;
            _jitter = 0.55f + 0.45f * std::sin( t * 0.71f ) ;
            _smoothness = 0.55f + 0.4f * std::sin( t * 0.57f + 1.0f ) ;
        }

        void_t update_variables( void_t ) noexcept
        {
            _frame_mode = _mode ;
            _frame_selected = _selected ;
            _frame_demo_step = _demo_step ;
            float_t const angle = _yaw * 0.01745329252f ;
            float_t const aspect = _width / _height ;
            _camera.make_orthographic( 30.0f * std::max( aspect, 1.0f ),
                30.0f / std::min( aspect, 1.0f ), 0.1f, 100.0f ) ;
            _camera.look_at( motor::math::vec3f_t( 28.0f * std::sin( angle ), 24.0f, 28.0f * std::cos( angle ) ),
                motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), motor::math::vec3f_t( 0.0f ) ) ;
            auto const matrix = _camera.get_proj_matrix() * _camera.get_view_matrix() ;
            for( size_t i = 0 ; i < noise_count ; ++i )
            {
                auto & b = _bindings[i] ;
                auto const r = tile_bounds( i ) ;
                b.matrix->set( matrix ) ;
                b.params->set( motor::math::vec4f_t( _time, _frequency, _amplitude, _gray ? 1.0f : 0.0f ) ) ;
                b.shape->set( motor::math::vec4f_t( r.w / r.h, _jitter, _smoothness, 0.0f ) ) ;
                b.rect->set( motor::math::vec4f_t( ( r.x + r.w * 0.5f ) * 2.0f / _width - 1.0f,
                    1.0f - ( r.y + r.h * 0.5f ) * 2.0f / _height, r.w / _width, r.h / _height ) ) ;
                b.octaves->set( _octaves ) ;
                b.mode->set( _frame_mode ) ;
            }
        }

    public:
        noise_app( options const opts, bool_t * passed ) noexcept : _options( opts ), _passed( passed ),
            _mode( opts.compare ? 1 : 0 ), _paused( opts.still ), _demo( opts.demo ) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            _grid = make_grid( "noise_terrain", 192 ) ;
            _quad = make_grid( "noise_quad", 1 ) ;
            {
                for( size_t i = 0 ; i < noise_count ; ++i )
                {
                    motor::graphics::msl_object_t shader( "noise_playground_" + motor::to_string( i ) ) ;
                    shader.add( motor::graphics::msl_api_type::msl_4_0, shader_source( i ) ) ;
                    shader.link_geometry( "noise_terrain" ) ;
                    shader.link_geometry( "noise_quad" ) ;
                    motor::graphics::variable_set_t vars ;
                    auto & b = _bindings[i] ;
                    b.matrix = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    b.params = vars.data_variable<motor::math::vec4f_t>( "u_params" ) ;
                    b.shape = vars.data_variable<motor::math::vec4f_t>( "u_shape" ) ;
                    b.rect = vars.data_variable<motor::math::vec4f_t>( "u_rect" ) ;
                    b.octaves = vars.data_variable<int_t>( "u_octaves" ) ;
                    b.mode = vars.data_variable<int_t>( "u_mode" ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                    _shaders[i] = motor::shared( std::move( shader ) ) ;
                }
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
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.045f, 0.055f, 0.065f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "noise_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) { _mode = 1 ; _selected = 0 ; }
            if( _demo ) animate_demo() ;
            update_variables() ;
            {
                motor::application::window_info_t info ;
                info.window_name = _options.d3d ? "motor | noise playground | D3D11" : "motor | noise playground | GL4" ;
                info.x = 20 ; info.y = 40 ; info.w = 1920 ; info.h = 1080 ;
                info.gen = _options.d3d ? motor::application::graphics_generation::gen4_d3d11 :
                    motor::application::graphics_generation::gen4_gl4 ;
                send_window_message( create_window( info ), []( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { true } ) ) ;
                } ) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused )
            {
                if( _demo && _ready )
                {
                    _demo_time += double( data.sec_dt ) ;
                    animate_demo() ;
                }
                _time += data.sec_dt * _speed ;
                if( _orbit ) _yaw = std::fmod( _yaw + data.sec_dt * 5.0f, 360.0f ) ;
            }
            update_variables() ;
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_grid ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_quad ) ;
            for( auto * shader : _shaders ) fe->configure<motor::graphics::msl_object_t>( shader ) ;
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto * shader : _shaders ) fe->release<motor::graphics::msl_object_t>( shader ) ;
            fe->release<motor::graphics::geometry_object_t>( &_grid ) ;
            fe->release<motor::graphics::geometry_object_t>( &_quad ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }

        void_t on_render( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->push( &_state ) ;
            bool_t ready = !_quitting ;
            for( auto * shader : _shaders )
            {
                auto const status = fe->decode( shader ) ;
                if( !_quitting && status.first != motor::graphics::object_state::in_transit &&
                    status.second == motor::graphics::result::failed )
                {
                    motor::log::global_t::test( "[noise] shader compilation", false ) ;
                    *_passed = false ;
                    _quitting = true ; close() ;
                }
                ready = ready && status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok ;
            }
            _ready = ready ;
            if( ready )
            {
                if( !_options.smoke ) *_passed = true ;
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.varset = 0 ;
                detail.geo = _frame_mode == 0 ? 0 : 1 ;
                if( _frame_mode == 0 ) fe->render( _shaders[_frame_selected], detail ) ;
                else for( auto * shader : _shaders ) fe->render( shader, detail ) ;
                ++_phase_draws ;
                if( _options.smoke && _demo )
                {
                    uint_t const bit = uint_t( 1 ) << _frame_demo_step ;
                    if( ( _demo_seen & bit ) == 0 )
                    {
                        motor::log::global_t::test( "[noise demo] " + motor::string_t(
                            _frame_mode == 1 ? "2D comparison" : names[_frame_selected] ), true ) ;
                        _demo_seen |= bit ;
                    }
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 14, 20 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 306, 370 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Noise playground", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::BeginDisabled( _options.smoke ) ;
                if( ImGui::Checkbox( "Demo", &_demo ) && _demo )
                {
                    _demo_time = 0.0 ; _demo_seen = 0 ;
                    animate_demo() ;
                }
                ImGui::SameLine() ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::BeginDisabled( _demo ) ;
                ImGui::RadioButton( "Landscape", &_mode, 0 ) ; ImGui::SameLine() ;
                ImGui::RadioButton( "2D comparison", &_mode, 1 ) ;
                ImGui::Combo( "Noise", &_selected, names, int_t( noise_count ) ) ;
                ImGui::SliderFloat( "Frequency", &_frequency, 0.3f, 8.0f ) ;
                ImGui::SliderFloat( "Amplitude", &_amplitude, 0.0f, 10.0f ) ;
                ImGui::SliderFloat( "Speed", &_speed, 0.0f, 2.0f ) ;
                ImGui::SliderInt( "Octaves", &_octaves, 1, 8 ) ;
                ImGui::SliderFloat( "IQ jitter", &_jitter, 0.0f, 1.0f ) ;
                ImGui::SliderFloat( "IQ smoothness", &_smoothness, 0.0f, 1.0f ) ;
                ImGui::SliderFloat( "Camera yaw", &_yaw, 0.0f, 360.0f ) ;
                ImGui::EndDisabled() ;
                ImGui::Checkbox( "Orbit", &_orbit ) ; ImGui::SameLine() ;
                ImGui::Checkbox( "Grayscale", &_gray ) ;
                ImGui::EndDisabled() ;
                ImGui::Text( "%.1f UI FPS", ImGui::GetIO().Framerate ) ;
            }
            ImGui::End() ;
            if( _frame_mode == 1 )
            {
                auto * draw = ImGui::GetForegroundDrawList() ;
                for( size_t i = 0 ; i < noise_count ; ++i )
                {
                    auto const r = tile_bounds( i ) ;
                    draw->AddText( ImVec2( r.x, r.y - 21.0f ), IM_COL32( 225, 233, 235, 255 ), names[i] ) ;
                }
            }
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            if( _demo )
            {
                if( _demo_time >= double( noise_count + 1 ) &&
                    _demo_seen == ( uint_t( 1 ) << ( noise_count + 1 ) ) - 1 )
                {
                    motor::log::global_t::test( "[noise demo] complete timed cycle", true ) ;
                    *_passed = true ; _quitting = true ; close() ;
                }
                return ;
            }
            if( _phase_draws < 30 ) return ;
            motor::log::global_t::test( "[noise] " + motor::string_t( _phase == 0 ? "2D comparison" : names[_selected] ), true ) ;
            _phase_draws = 0 ;
            if( ++_phase == noise_count + 1 )
            {
                *_passed = true ; _quitting = true ; close() ; return ;
            }
            _mode = 0 ; _selected = int_t( _phase - 1 ) ;
            _gray = ( _phase % 2 ) == 0 ;
            _octaves = _phase == 1 ? 1 : 5 ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 90 ) )
            {
                motor::log::global_t::test( "[noise] shader readiness / smoke timeout", false ) ;
                _quitting = true ; close() ;
            }
        }

        void_t on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.resize_changed && state.resize_msg.resize && state.resize_msg.w > 0 && state.resize_msg.h > 0 )
            {
                _width = float_t( state.resize_msg.w ) ; _height = float_t( state.resize_msg.h ) ;
            }
            if( state.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & b : _bindings ) b = bindings() ;
            for( auto & shader : _shaders ) motor::release( motor::move( shader ) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) opts.d3d = false ;
        else if( arg == "--d3d-only" ) opts.d3d = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--compare" ) opts.compare = true ;
        else if( arg == "--demo" ) opts.demo = true ;
        else
        {
            motor::log::global_t::status( "Usage: 20_noise_playground [--gl-only|--d3d-only] [--smoke] [--still] [--compare] [--demo]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
    if( opts.smoke && opts.demo && opts.still )
    {
        motor::log::global_t::error( "The timed demo smoke requires animation; omit --still." ) ;
        motor::log::global_t::deinit() ; return 2 ;
    }
#ifndef _WIN32
    if( opts.d3d )
    {
        motor::log::global_t::error( "D3D11 requires Windows." ) ;
        motor::log::global_t::deinit() ; return 2 ;
    }
#endif
    bool passed = false ;
    int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::noise_app( opts, &passed ) ) ) ;
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
