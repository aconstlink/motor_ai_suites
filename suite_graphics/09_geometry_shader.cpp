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

    class geometry_shader_app : public motor::application::app
    {
        struct vertex
        {
            motor::math::vec4f_t position ;
            motor::math::vec4f_t color ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            float_t aspect = 4.0f / 3.0f ;
            size_t draws = 0 ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::data_variable<motor::math::mat4f_t> * matrices[2] = { nullptr, nullptr } ;
            motor::graphics::data_variable<motor::math::vec4f_t> * effect = nullptr ;
        } ;

        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::msl_object_mtr_t _shaders[2] = { nullptr, nullptr } ;
        motor::graphics::state_object_t _state ;
        size_t _num_vertices = 0 ;
        size_t _phase = 0 ;
        int_t _mode = 1 ;
        float_t _time = 1.04719755f ;
        float_t _amount = 0.55f ;
        float_t _shrink = 0.88f ;
        bool_t _paused = false ;
        bool_t _quitting = false ;
        std::chrono::steady_clock::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }

        void_t update_variables( void_t ) noexcept
        {
            float_t const pulse = 0.5f + 0.5f * std::sin( _time * 1.5f ) ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                auto & window = _windows[i] ;
                float_t const width = 8.6f * ( window.aspect > 1.0f ? window.aspect : 1.0f ) ;
                float_t const height = 8.6f / ( window.aspect < 1.0f ? window.aspect : 1.0f ) ;
                window.camera.make_orthographic( width, height, 0.1f, 15.0f ) ;
                window.camera.look_at( motor::math::vec3f_t( 0.0f, 1.5f, 9.0f ),
                    motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), motor::math::vec3f_t( 0.0f ) ) ;
                auto const matrix = window.camera.get_proj_matrix() * window.camera.get_view_matrix() ;
                for( auto * variable : window.matrices ) variable->set( matrix ) ;
                window.effect->set( motor::math::vec4f_t( _mode == 0 ? 0.0f : _amount * pulse,
                    _mode == 0 ? 1.0f : _shrink, _mode == 2 ? 2.0f : 1.0f, 2.0f ) ) ;
            }
        }

        void_t open_window( size_t const index ) noexcept
        {
            bool_t const d3d = index == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | geometry shader | D3D11" : "motor | geometry shader | GL4" ;
            info.x = int_t( 30 + index * 680 ) ;
            info.y = 60 ;
            info.w = 660 ;
            info.h = 560 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[index].aspect = float_t( info.w ) / float_t( info.h ) ;
            _windows[index].id = create_window( info ) ;
            send_window_message( _windows[index].id, []( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { true } ) ) ;
            } ) ;
        }

    public:
        geometry_shader_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ), _paused( opts.still ) {}

        // Existing buffer resize helpers divide by the initial zero element count.
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            {
                // A subdivided cube projected onto a sphere; split vertices preserve flat face colors.
                motor::vector<vertex> vertices ;
                vertices.reserve( 6 * 4 * 4 * 6 ) ;
                motor::math::vec3f_t const corners[8] = {
                    {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                    {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} } ;
                uint_t const faces[6][4] = {
                    {4,5,6,7}, {1,0,3,2}, {0,4,7,3}, {5,1,2,6}, {3,7,6,2}, {0,1,5,4} } ;
                uint_t const indices[6] = { 0, 1, 2, 0, 2, 3 } ;
                for( size_t f = 0 ; f < 6 ; ++f )
                {
                    auto const a = corners[faces[f][0]] ;
                    auto const u = corners[faces[f][1]] - a ;
                    auto const v = corners[faces[f][3]] - a ;
                    for( size_t y = 0 ; y < 4 ; ++y )
                    {
                        for( size_t x = 0 ; x < 4 ; ++x )
                        {
                            motor::math::vec3f_t quad[4] ;
                            size_t const dx[4] = {0,1,1,0}, dy[4] = {0,0,1,1} ;
                            for( size_t k = 0 ; k < 4 ; ++k )
                                quad[k] = ( a + u * ( float_t( x + dx[k] ) / 4.0f ) +
                                    v * ( float_t( y + dy[k] ) / 4.0f ) ).normalized() ;
                            for( size_t t = 0 ; t < 2 ; ++t )
                            {
                                auto const p = quad[indices[t*3]] ;
                                auto const q = quad[indices[t*3+1]] ;
                                auto const r = quad[indices[t*3+2]] ;
                                auto const n = ( q - p ).cross( r - p ).normalized() ;
                                float_t const light = 0.3f + 0.7f * std::fmax( 0.0f,
                                    n.dot( motor::math::vec3f_t( -0.4f, 0.8f, 0.6f ).normalized() ) ) ;
                                motor::math::vec4f_t const color( ( 0.4f + 0.25f*n.x() ) * light,
                                    ( 0.6f + 0.2f*n.y() ) * light, ( 0.65f + 0.25f*n.z() ) * light, 1.0f ) ;
                                for( size_t k = 0 ; k < 3 ; ++k )
                                    vertices.push_back( { motor::math::vec4f_t( quad[indices[t*3+k]], 1.0f ), color } ) ;
                            }
                        }
                    }
                }
                _num_vertices = vertices.size() ;
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::vertex_attribute::color0,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                vb.resize( vertices.size() ).update<vertex>( [&]( vertex * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = vertices[i] ;
                } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
                ib.resize( vertices.size() ).update<uint_t>( []( uint_t * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = uint_t( i ) ;
                } ) ;
                _geometry = motor::graphics::geometry_object_t( "gs_sphere",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                motor::graphics::msl_object_t reference( "gs_reference" ) ;
                reference.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config gs_reference
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            mat4_t u_view_projection ;
                            void main()
                            {
                                out.pos = u_view_projection * vec4_t( in.pos.xyz + vec3_t( -2.0, 0.0, 0.0 ), 1.0 ) ;
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
                motor::graphics::msl_object_t effect( "gs_effect" ) ;
                // Current generators need named GS input indices and no cross() builtin.
                effect.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config gs_effect
                    {
                        vertex_shader
                        {
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            void main() { out.pos = in.pos ; out.color = in.color ; }
                        }
                        geometry_shader
                        {
                            in triangles ;
                            out triangles[ max_verts = 6 ] ;
                            inout vec4_t pos : position ;
                            inout vec4_t color : color ;
                            mat4_t u_view_projection ;
                            vec4_t u_effect ;
                            void main()
                            {
                                int ia = 0 ;
                                int ib = 1 ;
                                int ic = 2 ;
                                vec3_t a = in[ia].pos.xyz ;
                                vec3_t b = in[ib].pos.xyz ;
                                vec3_t c = in[ic].pos.xyz ;
                                vec3_t center = ( a + b + c ) / 3.0 ;
                                vec3_t ab = b - a ;
                                vec3_t ac = c - a ;
                                vec3_t normal = normalize( vec3_t( ab.y * ac.z - ab.z * ac.y,
                                    ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x ) ) ;
                                for( int layer = 0 ; layer < 2 ; ++layer )
                                {
                                    if( layer < u_effect.z )
                                    {
                                        float_t shift = u_effect.x + float_t( layer ) * 0.28 ;
                                        for( int i = 0 ; i < 3 ; ++i )
                                        {
                                            vec3_t p = center + ( in[i].pos.xyz - center ) * u_effect.y + normal * shift ;
                                            out.pos = u_view_projection * vec4_t( p + vec3_t( u_effect.w, 0.0, 0.0 ), 1.0 ) ;
                                            out.color = vec4_t( in[i].color.xyz * ( 1.0 - float_t( layer ) * 0.35 ), 1.0 ) ;
                                            emit_vertex() ;
                                        }
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
                _shaders[0] = motor::shared( std::move( reference ) ) ;
                _shaders[1] = motor::shared( std::move( effect ) ) ;
                for( size_t s = 0 ; s < 2 ; ++s )
                {
                    _shaders[s]->link_geometry( "gs_sphere" ) ;
                    for( size_t w = 0 ; w < window_count() ; ++w )
                    {
                        motor::graphics::variable_set_t vars ;
                        _windows[w].matrices[s] = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                        if( s == 1 ) _windows[w].effect = vars.data_variable<motor::math::vec4f_t>( "u_effect" ) ;
                        _shaders[s]->add_variable_set( motor::shared( std::move( vars ) ) ) ;
                    }
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
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.025f, 0.035f, 0.045f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "gs_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) { _mode = 0 ; _paused = true ; }
            update_variables() ;
            for( size_t w = 0 ; w < window_count() ; ++w ) open_window( w ) ;
            motor::log::global_t::status( "[geometry shader] 192 input triangles per draw; reference + GS; modes: pass-through, explode, double shell" ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused ) _time += data.sec_dt ;
            update_variables() ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            size_t const index = id == _windows[0].id ? 0 : 1 ;
            if( data.last_frame )
            {
                for( auto shader : _shaders ) fe->release<motor::graphics::msl_object_t>( shader ) ;
                fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
                fe->release<motor::graphics::state_object_t>( &_state ) ;
                return ;
            }
            if( data.first_frame )
            {
                fe->configure<motor::graphics::state_object_t>( &_state ) ;
                fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
                for( auto shader : _shaders ) fe->configure<motor::graphics::msl_object_t>( shader ) ;
            }
            fe->push( &_state ) ;
            bool_t ready = true ;
            for( auto shader : _shaders )
            {
                auto const status = fe->decode( shader ) ;
                if( status.second == motor::graphics::result::failed && !_quitting )
                {
                    _quitting = true ;
                    motor::log::global_t::error( "[geometry shader] shader configuration failed on window " +
                        motor::to_string( index ) ) ;
                    close() ;
                }
                ready = ready && status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok ;
            }
            if( ready && !_quitting )
            {
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.varset = index ;
                detail.num_elems = _num_vertices ;
                // Alternate the order to exercise enabling and disabling the GS between draws.
                size_t const first = _phase % 2 ;
                fe->render( _shaders[first], detail ) ;
                fe->render( _shaders[1-first], detail ) ;
                ++_windows[index].draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 44 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 330, 180 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Geometry shader", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::Text( "Input: 192 | GS output: %d triangles", _mode == 2 ? 384 : 192 ) ;
                ImGui::BeginDisabled( _options.smoke ) ;
                ImGui::Combo( "Mode", &_mode, "Pass-through\0Explode\0Double shell\0" ) ;
                ImGui::SliderFloat( "Distance", &_amount, 0.0f, 1.0f ) ;
                ImGui::SliderFloat( "Face scale", &_shrink, 0.25f, 1.0f ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w ) if( _windows[w].draws < 90 ) return ;
            motor::log::global_t::status( "[geometry shader] completed mode " + motor::to_string( _mode ) +
                " on every backend; draw order " + motor::to_string( _phase % 2 ) ) ;
            if( ++_phase == 6 )
            {
                *_passed = true ;
                _quitting = true ;
                motor::log::global_t::status( "[geometry shader] smoke passed; pixels require visual verification" ) ;
                close() ;
                return ;
            }
            _mode = int_t( _phase % 3 ) ;
            for( auto & window : _windows ) window.draws = 0 ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 60 ) )
            {
                _quitting = true ;
                motor::log::global_t::error( "[geometry shader] smoke timed out" ) ;
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
                for( auto & matrix : window.matrices ) matrix = nullptr ;
                window.effect = nullptr ;
            }
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
            motor::log::global_t::status( "Usage: 09_geometry_shader [--gl-only|--d3d-only] [--smoke] [--still]" ) ;
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
            motor::shared( sample::geometry_shader_app( opts, &passed ) ) ) ;
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
