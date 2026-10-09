#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/array_object.h>
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
#include <cstdlib>

namespace sample
{
    using namespace motor::core::types ;

    struct options
    {
        bool_t dual = true ;
        bool_t d3d_only = false ;
        bool_t smoke = false ;
        bool_t still = false ;
        bool_t vsync = true ;
        size_t side = 48 ;
        size_t subdivisions = 8 ;
    } ;

    class pulling_field_app : public motor::application::app
    {
        struct record
        {
            motor::math::vec4f_t first ;
            motor::math::vec4f_t second ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            size_t revision = size_t( -1 ) ;
            size_t phase_draws = 0 ;
            size_t updates = 0 ;
            float_t aspect = 4.0f / 3.0f ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::data_variable<motor::math::mat4f_t> * view = nullptr ;
            motor::graphics::data_variable<motor::math::vec4f_t> * animation = nullptr ;
        } ;

        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::array_object_t _mesh ;
        motor::graphics::array_object_t _objects ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        motor::graphics::state_object_t _state ;
        size_t _revision = 0 ;
        size_t _palette = 0 ;
        size_t _phase = 0 ;
        int_t _active = 0 ;
        int_t _draw_count = 0 ;
        bool_t _palette_dirty = true ;
        bool_t _paused = false ;
        bool_t _orbit = true ;
        bool_t _quitting = false ;
        float_t _time = 0.0f ;
        float_t _yaw = 35.0f ;
        float_t _amplitude = 1.8f ;
        float_t _speed = 1.0f ;
        std::chrono::steady_clock::time_point _started ;

        size_t capacity( void_t ) const noexcept { return _options.side * _options.side ; }
        size_t window_count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }
        size_t mesh_vertices( void_t ) const noexcept
        {
            size_t const edge = _options.subdivisions + 1 ;
            return 6 * edge * edge ;
        }
        size_t mesh_indices( void_t ) const noexcept
        {
            return 36 * _options.subdivisions * _options.subdivisions ;
        }
        size_t geometry_bytes( void_t ) const noexcept
        {
            return capacity() * ( mesh_vertices() * sizeof( motor::math::vec3f_t ) +
                mesh_indices() * sizeof( uint_t ) ) ;
        }

        void_t update_camera( window_data & window ) noexcept
        {
            float_t const angle = _yaw * 0.01745329252f ;
            float_t const c = std::cos( angle ), s = std::sin( angle ) ;
            float_t const cp = std::cos( 0.75f ), sp = std::sin( 0.75f ) ;
            // Include the maximum wave height even for a small --side value.
            float_t const extent = float_t( _options.side < 8 ? 8 : _options.side ) ;
            float_t const span = 2.0f * extent / 1.45f ;
            float_t const aspect = window.aspect ;
            float_t const width = span * ( aspect > 1.0f ? aspect : 1.0f ) ;
            float_t const height = span / ( aspect < 1.0f ? aspect : 1.0f ) ;
            float_t const distance = 3.0f * extent ;
            motor::math::vec3f_t const target( 0.0f, 0.10f * extent, 0.0f ) ;
            motor::math::vec3f_t const eye = target +
                motor::math::vec3f_t( -cp*s*distance, sp*distance, cp*c*distance ) ;
            window.camera.make_orthographic( width, height, 0.1f, 4.0f * extent ) ;
            window.camera.look_at( eye, motor::math::vec3f_t( 0.0f, 1.0f, 0.0f ), target ) ;
            window.view->set( window.camera.get_proj_matrix() * window.camera.get_view_matrix() ) ;
        }

        void_t update_palette( void_t ) noexcept
        {
            _objects.data_buffer().update<record>( [&]( record * ptr, size_t const n )
            {
                float_t const half = float_t( _options.side - 1 ) * 0.5f ;
                for( size_t i = 0 ; i < n ; ++i )
                {
                    float_t const x = float_t( i % _options.side ) - half ;
                    float_t const z = float_t( i / _options.side ) - half ;
                    float_t const t = float_t( i % _options.side ) / float_t( _options.side - 1 ) ;
                    motor::math::vec4f_t color ;
                    if( _palette % 3 == 0 ) color = motor::math::vec4f_t( 0.15f + 0.8f*t, 0.72f - 0.4f*t, 0.85f - 0.65f*t, 1.0f ) ;
                    else if( _palette % 3 == 1 ) color = motor::math::vec4f_t( 0.65f, 0.20f + 0.65f*t, 0.85f - 0.7f*t, 1.0f ) ;
                    else color = motor::math::vec4f_t( 0.90f - 0.75f*t, 0.30f + 0.55f*t, 0.20f + 0.6f*t, 1.0f ) ;
                    ptr[i] = { motor::math::vec4f_t( x, 0.0f, z, 0.36f ), color } ;
                }
            } ) ;
            ++_revision ;
            _palette_dirty = false ;
        }

        void_t open_window( size_t const index ) noexcept
        {
            bool_t const d3d = index == 1 || _options.d3d_only ;
            motor::application::window_info_t info ;
            info.window_name = d3d ? "motor | pulling field | D3D11" : "motor | pulling field | GL4" ;
            info.x = int_t( 40 + index * 690 ) ;
            info.y = 80 ;
            info.w = 680 ;
            info.h = 600 ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4 ;
            _windows[index].aspect = float_t( info.w ) / float_t( info.h ) ;
            _windows[index].id = create_window( info ) ;
            send_window_message( _windows[index].id, [vsync = _options.vsync]( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) ) ;
                window.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
            } ) ;
        }

    public:
        pulling_field_app( options const opts, bool_t * passed ) noexcept :
            _options( opts ), _passed( passed ), _active( int_t( opts.side * opts.side ) ),
            _paused( opts.still ) {}

        // Existing vertex/index resize helpers compute a floating-point ratio with an initial count of zero.
#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            // Subdivide each face, then project its edge region onto a rounded box.
            {
                auto buffer = motor::graphics::data_buffer_t()
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                buffer.resize( mesh_vertices() ).update<record>( [&]( record * ptr, size_t )
                {
                    float_t const p[8][3] = {
                        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
                        {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1} } ;
                    uint_t const faces[6][4] = {
                        {4,5,6,7}, {1,0,3,2}, {0,4,7,3},
                        {5,1,2,6}, {3,7,6,2}, {0,1,5,4} } ;
                    size_t const edge = _options.subdivisions + 1 ;
                    for( size_t f = 0 ; f < 6 ; ++f )
                    {
                        for( size_t y = 0 ; y < edge ; ++y )
                        {
                            for( size_t x = 0 ; x < edge ; ++x )
                            {
                                float_t const u = float_t( x ) / float_t( _options.subdivisions ) ;
                                float_t const v = float_t( y ) / float_t( _options.subdivisions ) ;
                                float_t core[3], normal[3], position[3] ;
                                float_t length_squared = 0.0f ;
                                for( size_t axis = 0 ; axis < 3 ; ++axis )
                                {
                                    float_t const a = p[faces[f][0]][axis] ;
                                    float_t const q = a + u * ( p[faces[f][1]][axis] - a ) +
                                        v * ( p[faces[f][3]][axis] - a ) ;
                                    core[axis] = q < -0.65f ? -0.65f : ( q > 0.65f ? 0.65f : q ) ;
                                    normal[axis] = q - core[axis] ;
                                    length_squared += normal[axis] * normal[axis] ;
                                }
                                float_t const inverse_length = 1.0f / std::sqrt( length_squared ) ;
                                for( size_t axis = 0 ; axis < 3 ; ++axis )
                                {
                                    normal[axis] *= inverse_length ;
                                    position[axis] = core[axis] + 0.35f * normal[axis] ;
                                }
                                ptr[f*edge*edge+y*edge+x] = {
                                    motor::math::vec4f_t( position[0], position[1], position[2], 1.0f ),
                                    motor::math::vec4f_t( normal[0], normal[1], normal[2], 0.0f ) } ;
                            }
                        }
                    }
                } ) ;
                _mesh = motor::graphics::array_object_t( "field_mesh", std::move( buffer ) ) ;
            }
            // Object records are compact; geometry only carries index IDs and an unused dummy VB.
            {
                auto buffer = motor::graphics::data_buffer_t()
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 )
                    .add_layout_element( motor::graphics::type::tfloat, motor::graphics::type_struct::vec4 ) ;
                buffer.resize( capacity() ) ;
                _objects = motor::graphics::array_object_t( "field_objects", std::move( buffer ) ) ;
                update_palette() ;
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 ) ;
                vb.resize( capacity() * mesh_vertices() ).update<motor::math::vec3f_t>( []( auto * ptr, size_t const n )
                {
                    for( size_t i = 0 ; i < n ; ++i ) ptr[i] = motor::math::vec3f_t( 0.0f ) ;
                } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
                ib.resize( capacity() * mesh_indices() ).update<uint_t>( [&]( uint_t * ptr, size_t )
                {
                    size_t const edge = _options.subdivisions + 1 ;
                    uint_t const pattern[6] = { uint_t( edge + 1 ), 0, 1, 0,
                        uint_t( edge + 1 ), uint_t( edge ) } ;
                    size_t cursor = 0 ;
                    for( size_t object = 0 ; object < capacity() ; ++object )
                    {
                        for( size_t face = 0 ; face < 6 ; ++face )
                        {
                            for( size_t y = 0 ; y < _options.subdivisions ; ++y )
                            {
                                for( size_t x = 0 ; x < _options.subdivisions ; ++x )
                                {
                                    uint_t const base = uint_t( object * mesh_vertices() +
                                        face * edge * edge + y * edge + x ) ;
                                    for( size_t i = 0 ; i < 6 ; ++i ) ptr[cursor++] = base + pattern[i] ;
                                }
                            }
                        }
                    }
                } ) ;
                _geometry = motor::graphics::geometry_object_t( "field_geometry",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "pulling_field" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config pulling_field
                    {
                        vertex_shader
                        {
                            uint_t vid : vertex_id ;
                            data_buffer_t u_mesh ;
                            data_buffer_t u_objects ;
                            mat4_t u_view_projection ;
                            vec4_t u_animation ;
                            int_t u_mesh_vertices ;
                            out vec4_t pos : position ;
                            out vec3_t normal : texcoord0 ;
                            out vec4_t color : color ;
                            void main()
                            {
                                int_t object_id = vid / u_mesh_vertices ;
                                int_t vertex_id = vid % u_mesh_vertices ;
                                vec4_t local = fetch_data( u_mesh, vertex_id * 2 ) ;
                                vec4_t normal = fetch_data( u_mesh, vertex_id * 2 + 1 ) ;
                                vec4_t object = fetch_data( u_objects, object_id * 2 ) ;
                                vec4_t color = fetch_data( u_objects, object_id * 2 + 1 ) ;
                                float_t phase = object.x * 0.22 + object.z * 0.18 ;
                                float_t angle = 0.28 * sin( u_animation.x + phase ) ;
                                float_t c = cos( angle ) ;
                                float_t s = sin( angle ) ;
                                vec3_t rotated = vec3_t( c * local.x + s * local.z, local.y, c * local.z - s * local.x ) ;
                                vec3_t n = vec3_t( c * normal.x + s * normal.z, normal.y, c * normal.z - s * normal.x ) ;
                                float_t wave = u_animation.y * sin( phase + u_animation.x ) ;
                                vec3_t position = object.xyz + rotated * object.w + vec3_t( 0.0, wave, 0.0 ) ;
                                out.pos = u_view_projection * vec4_t( position, 1.0 ) ;
                                out.normal = n ;
                                out.color = color ;
                            }
                        }
                        pixel_shader
                        {
                            in vec3_t normal : texcoord0 ;
                            in vec4_t color : color ;
                            out vec4_t color : color ;
                            void main()
                            {
                                vec3_t n = normalize( in.normal ) ;
                                float_t key = max( dot( n, normalize( vec3_t( -0.5, 1.0, 0.7 ) ) ), 0.0 ) ;
                                float_t fill = max( dot( n, normalize( vec3_t( 0.8, 0.2, -0.6 ) ) ), 0.0 ) ;
                                vec3_t color = in.color.xyz * ( 0.22 + 0.68 * key ) + vec3_t( 0.10, 0.13, 0.18 ) * fill ;
                                out.color = vec4_t( color, 1.0 ) ;
                            }
                        }
                    }
                )" ) ;
                shader.link_geometry( "field_geometry" ) ;
                for( size_t i = 0 ; i < window_count() ; ++i )
                {
                    motor::graphics::variable_set_t vars ;
                    vars.array_variable( "u_mesh" )->set( "field_mesh" ) ;
                    vars.array_variable( "u_objects" )->set( "field_objects" ) ;
                    vars.data_variable<int_t>( "u_mesh_vertices" )->set( int_t( mesh_vertices() ) ) ;
                    _windows[i].view = vars.data_variable<motor::math::mat4f_t>( "u_view_projection" ) ;
                    _windows[i].animation = vars.data_variable<motor::math::vec4f_t>( "u_animation" ) ;
                    update_camera( _windows[i] ) ;
                    _windows[i].animation->set( motor::math::vec4f_t( 0.0f, _amplitude, 0.0f, 0.0f ) ) ;
                    shader.add_variable_set( motor::shared( std::move( vars ) ) ) ;
                }
                _shader = motor::shared( std::move( shader ) ) ;
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
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.035f, 0.04f, 0.045f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "field_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            if( _options.smoke ) _active = 1 ;
            for( size_t i = 0 ; i < window_count() ; ++i ) open_window( i ) ;
            motor::log::global_t::status( "[pulling field] capacity: " + motor::to_string( capacity() ) +
                " cubes; two array bindings; one scene draw per window" ) ;
            motor::log::global_t::status( "[pulling field] subdivisions: " + motor::to_string( _options.subdivisions ) +
                "; triangles per cube: " + motor::to_string( mesh_indices() / 3 ) +
                "; triangles at capacity per window: " + motor::to_string( capacity() * mesh_indices() / 3 ) +
                "; dummy VB + IB bytes per backend: " + motor::to_string( geometry_bytes() ) ) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused )
            {
                _time += data.sec_dt * _speed ;
                if( _orbit ) _yaw = std::fmod( _yaw + data.sec_dt * 6.0f, 360.0f ) ;
            }
            if( _palette_dirty ) update_palette() ;
            _draw_count = _active ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                update_camera( _windows[i] ) ;
                _windows[i].animation->set( motor::math::vec4f_t( _time, _amplitude, 0.0f, 0.0f ) ) ;
            }
        }

        void_t on_first_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->configure<motor::graphics::array_object_t>( &_mesh ) ;
            fe->configure<motor::graphics::array_object_t>( &_objects ) ;
            fe->configure<motor::graphics::msl_object_t>( _shader ) ;
        }

        void_t on_last_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
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
            fe->push( &_state ) ;
            auto const status = fe->decode( _shader ) ;
            if( !_quitting && status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok )
            {
                if( window.revision != _revision )
                {
                    fe->update( &_objects ) ;
                    window.revision = _revision ;
                    ++window.updates ;
                }
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.varset = index ;
                detail.num_elems = size_t( _draw_count ) * mesh_indices() ;
                fe->render( _shader, detail ) ;
                ++window.phase_draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 12, 44 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowSize( ImVec2( 350, 270 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Vertex pulling", nullptr, ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::Text( "%d cubes | %zu triangles", _draw_count, size_t( _draw_count ) * mesh_indices() / 3 ) ;
                ImGui::Text( "%zu vertices | %zu subdivisions", size_t( _draw_count ) * mesh_vertices(), _options.subdivisions ) ;
                ImGui::Text( "VB + IB capacity: %.1f MiB / backend", double( geometry_bytes() ) / ( 1024.0 * 1024.0 ) ) ;
                ImGui::TextUnformatted( "1 scene draw | 2 array buffers" ) ;
                ImGui::Text( "%.1f UI FPS | VSync %s", ImGui::GetIO().Framerate, _options.vsync ? "on" : "off" ) ;
                ImGui::BeginDisabled( _options.smoke ) ;
                ImGui::SliderInt( "Cubes", &_active, 1, int_t( capacity() ) ) ;
                ImGui::SliderFloat( "Wave", &_amplitude, 0.0f, 3.0f ) ;
                ImGui::SliderFloat( "Speed", &_speed, 0.1f, 3.0f ) ;
                ImGui::SliderFloat( "Camera", &_yaw, 0.0f, 360.0f ) ;
                ImGui::Checkbox( "Pause", &_paused ) ;
                ImGui::SameLine() ;
                ImGui::Checkbox( "Orbit", &_orbit ) ;
                if( ImGui::Button( "Next palette" ) ) { ++_palette ; _palette_dirty = true ; }
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                if( _windows[i].phase_draws < 90 || _windows[i].updates < _phase + 1 ) return ;
            }
            motor::log::global_t::status( "[pulling field] completed phase " + motor::to_string( _phase ) +
                ": " + motor::to_string( _draw_count ) + " cubes, " +
                motor::to_string( size_t( _draw_count ) * mesh_indices() / 3 ) + " triangles per window" ) ;
            if( ++_phase == 4 )
            {
                *_passed = true ;
                _quitting = true ;
                motor::log::global_t::status( "[pulling field] smoke passed; pixels require visual verification" ) ;
                close() ;
                return ;
            }
            _active = _phase == 1 ? int_t( capacity() / 4 ) :
                ( _phase == 2 ? int_t( capacity() ) : int_t( _options.side ) ) ;
            ++_palette ;
            _palette_dirty = true ;
            for( auto & window : _windows ) window.phase_draws = 0 ;
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
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 120 ) )
            {
                _quitting = true ;
                motor::log::global_t::error( "[pulling field] smoke timed out" ) ;
                close() ;
            }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( auto & window : _windows ) { window.view = nullptr ; window.animation = nullptr ; }
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
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( ( arg == "--side" || arg == "--subdivisions" ) && i + 1 < argc )
        {
            char * end = nullptr ;
            auto const n = std::strtoul( argv[++i], &end, 10 ) ;
            auto const minimum = arg == "--side" ? 4ul : 1ul ;
            auto const maximum = arg == "--side" ? 96ul : 16ul ;
            if( end == argv[i] || *end != '\0' || n < minimum || n > maximum )
            {
                motor::log::global_t::error( arg + " must be an integer in [" +
                    motor::to_string( minimum ) + ", " + motor::to_string( maximum ) + "]" ) ;
                motor::log::global_t::deinit() ;
                return 2 ;
            }
            if( arg == "--side" ) opts.side = size_t( n ) ;
            else opts.subdivisions = size_t( n ) ;
        }
        else
        {
            motor::log::global_t::status( "Usage: 07_vertex_pulling_field [--gl-only|--d3d-only] [--side 4..96] [--subdivisions 1..16] [--no-vsync] [--smoke] [--still]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
    {
        size_t const edge = opts.subdivisions + 1 ;
        size_t const bytes = opts.side * opts.side * ( 6 * edge * edge * sizeof( motor::math::vec3f_t ) +
            36 * opts.subdivisions * opts.subdivisions * sizeof( motor::core::types::uint_t ) ) ;
        if( bytes > 256 * 1024 * 1024 )
        {
            motor::log::global_t::error( "VB + IB exceed 256 MiB per backend; reduce --side or --subdivisions." ) ;
            motor::log::global_t::deinit() ;
            return 2 ;
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
            motor::shared( sample::pulling_field_app( opts, &passed ) ) ) ;
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
