#include "capture_options.hpp"
#include "frustum_intersections_checks.hpp"
#include "scene_bounds.hpp"
#include "frustum_stress.hpp"
#include <motor/platform/global.h>
#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/io/global.h>
#include <motor/profiling/global.h>
#include <motor/memory/global.h>
#include <atomic>
#include <cassert>
#include <chrono>

namespace intersection_sample
{
    using frontend_t = motor::graphics::gen4::frontend_ptr_t ;
    using primitives_t = motor::gfx::primitive_render_3d_t ;

    struct options
    {
        sample_support::capture_options capture ;
        bool_t d3d = false, dual = false, cpu = false, demo = false, still = false ;
        bool_t ui = true, vsync = true, smoke = false, scene_bounds = false ;
    } ;

    vec4_t color_for( result_t const result ) noexcept
    { return result == result_t::inside ? vec4_t( .16f, .85f, .66f, 1 ) : vec4_t( 1, .48f, .16f, 1 ) ; }

    void_t triangle( primitives_t & renderer, vec3_t const & a, vec3_t const & b,
        vec3_t const & c, vec4_t const & color )
    {
        auto const normal = ( b - a ).crossed( c - a ) ;
        if( normal.length2() < 1e-12f ) return ;
        float_t const light = .28f + .72f * std::abs( normal.normalized().dot( vec3_t( .36f, .8f, .48f ) ) ) ;
        renderer.draw_tri( a, b, c, vec4_t( color.xyz() * light, 1 ) ) ;
    }

    void_t box_mesh( primitives_t & renderer, corners_t const & p, vec4_t const & color )
    {
        size_t const faces[6][4] = { {0,1,2,3}, {4,7,6,5}, {0,4,5,1},
            {3,2,6,7}, {1,5,6,2}, {0,3,7,4} } ;
        for( auto const & f : faces )
        {
            triangle( renderer, p[f[0]], p[f[1]], p[f[2]], color ) ;
            triangle( renderer, p[f[0]], p[f[2]], p[f[3]], color ) ;
        }
    }

    void_t point_marker( primitives_t & renderer, vec3_t const & center, float_t const radius,
        vec4_t const & color )
    {
        vec3_t const ring[4] = { vec3_t( radius, 0, 0 ), vec3_t( 0, 0, radius ),
            vec3_t( -radius, 0, 0 ), vec3_t( 0, 0, -radius ) } ;
        for( size_t i = 0 ; i < 4 ; ++i )
        {
            triangle( renderer, center + vec3_t( 0, radius, 0 ), center + ring[i], center + ring[(i+1)%4], color ) ;
            triangle( renderer, center - vec3_t( 0, radius, 0 ), center + ring[(i+1)%4], center + ring[i], color ) ;
        }
    }

    void_t draw_object( primitives_t & solid, primitives_t & wire, object const & obj, bool_t const bounds )
    {
        auto const color = color_for( obj.result ) ;
        auto const edge = vec4_t( color.xyz() * .72f + vec3_t( .20f ), 1 ) ;
        switch( obj.type )
        {
        case shape::sphere:
        {
            auto const vertex = [&]( size_t const y, size_t const x )
            {
                float_t const latitude = float_t( y ) * pi / 8, longitude = float_t( x ) * 2 * pi / 16 ;
                return obj.center + vec3_t( std::sin( latitude ) * std::cos( longitude ),
                    std::cos( latitude ), std::sin( latitude ) * std::sin( longitude ) ) * obj.radius ;
            } ;
            for( size_t y = 0 ; y < 8 ; ++y ) for( size_t x = 0 ; x < 16 ; ++x )
            {
                auto const a = vertex( y, x ), b = vertex( y+1, x ), c = vertex( y+1, x+1 ), d = vertex( y, x+1 ) ;
                triangle( solid, a, b, c, color ) ; triangle( solid, a, c, d, color ) ;
            }
            if( bounds ) wire.draw_wire_sphere( obj.center, obj.radius * 1.002f, edge, 32 ) ;
            break ;
        }
        case shape::aabb:
        case shape::obb:
        {
            auto const p = obj.type == shape::aabb ? hit_t::aabb_t( obj.center + obj.extent, obj.center - obj.extent ).points() :
                hit_t::obb_t( obj.center, obj.extent, obj.axes ).points() ;
            box_mesh( solid, p, color ) ;
            if( bounds ) wire.draw_wire_cube( p, edge ) ;
            break ;
        }
        case shape::ray:
            // A ray is infinite; show its origin, its first hit and a short continuation.
            wire.draw_arrow( obj.center, obj.direction, obj.hit_parameter + .7f, color, .28f, .10f ) ;
            point_marker( solid, obj.center, .10f, edge ) ;
            point_marker( solid, obj.center + obj.direction * obj.hit_parameter, .18f, vec4_t( 1 ) ) ;
            break ;
        case shape::point:
            point_marker( solid, obj.center, .17f, color ) ;
            break ;
        }
    }

    class intersection_app : public motor::application::app
    {
        struct view_data
        {
            camera_t camera ;
            primitives_t solid, wire ;
            motor::graphics::state_object_t state ;
            rectangle area ;
            motor::vector<size_t> submitted ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            uint_t width = 1280, height = 800 ;
            view_data views[2] ;
            scene data ;
            bounds_scene hierarchy ;
            primitives_t guide ;
            motor::graphics::state_object_t clear, overlay ;
            std::atomic_size_t completed { 0 } ;
            bool_t dirty = true, prepared = false ;
        } ;
        options _opts ;
        bool_t * _passed ;
        window_data _windows[2] ;
        bool_t _demo = false, _paused = false, _bounds = true, _ortho = false, _quitting = false ;
        bool_t _group_bounds = true, _root_bounds = false ;
        float_t _time = 0, _yaw = 0, _fov = .82f, _far = 18, _speed = 1 ;
        int_t _mode = 0, _active_mode = 0 ;
        size_t _phase = 0 ;
        std::chrono::steady_clock::time_point _started ;

        size_t count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        window_data & window( window_id_t const id ) noexcept { return _windows[id == _windows[0].id ? 0 : 1] ; }

        void_t make_states( window_data & w, motor::string_cref_t name )
        {
            {
                motor::graphics::render_state_sets_t s ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ;
                s.clear_s.ss.do_color_clear = true ; s.clear_s.ss.do_depth_clear = true ;
                s.clear_s.ss.clear_color = vec4_t( .027f, .030f, .033f, 1 ) ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = true ;
                s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.scissor_s.do_change = true ; s.scissor_s.ss.do_activate = false ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
                s.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, w.width, w.height ) ;
                w.clear = motor::graphics::state_object_t( name + ".clear" ) ; w.clear.add_render_state_set( s ) ;
            }
            for( size_t v = 0 ; v < 2 ; ++v )
            {
                motor::graphics::render_state_sets_t s ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
                s.scissor_s.do_change = true ; s.scissor_s.ss.do_activate = true ;
                w.views[v].state = motor::graphics::state_object_t( name + ".viewport." + motor::to_string( v ) ) ;
                w.views[v].state.add_render_state_set( s ) ;
            }
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = false ; s.depth_s.ss.do_depth_write = false ;
                w.overlay = motor::graphics::state_object_t( name + ".overlay" ) ; w.overlay.add_render_state_set( s ) ;
            }
        }

        void_t fill( window_data & w )
        {
            for( auto & view : w.views )
            {
                view.submitted.clear() ;
                auto & s = view.solid ; auto & lines = view.wire ;
                vec4_t const floor( .105f, .115f, .12f, 1 ) ;
                s.draw_tri( vec3_t( -11, 0, -9 ), vec3_t( 11, 0, -9 ), vec3_t( 11, 0, 9 ), floor ) ;
                s.draw_tri( vec3_t( -11, 0, -9 ), vec3_t( 11, 0, 9 ), vec3_t( -11, 0, 9 ), floor ) ;
                for( int_t i = -10 ; i <= 10 ; ++i )
                    lines.draw_line( vec3_t( float_t( i ), .01f, -9 ), vec3_t( float_t( i ), .01f, 9 ), vec4_t( .21f, .23f, .24f, 1 ) ) ;
                for( int_t i = -9 ; i <= 9 ; ++i )
                    lines.draw_line( vec3_t( -11, .01f, float_t( i ) ), vec3_t( 11, .01f, float_t( i ) ), vec4_t( .21f, .23f, .24f, 1 ) ) ;
                // Both views consume exactly this list. The observer never culls independently.
                for( size_t const index : w.data.visible )
                {
                    bool_t const scene_mode = _active_mode == 6 ;
                    draw_object( s, lines, w.data.objects[index], _bounds && !scene_mode ) ;
                    if( scene_mode && _bounds )
                        lines.draw_wire_cube( world_bounds( w.hierarchy.leaves[index] ).points(), vec4_t( .82f, .9f, .95f, 1 ) ) ;
                    view.submitted.emplace_back( index ) ;
                }
                s.set_view_proj( view.camera.mat_view(), view.camera.mat_proj() ) ;
                lines.set_view_proj( view.camera.mat_view(), view.camera.mat_proj() ) ;
                s.prepare_for_rendering() ; lines.prepare_for_rendering() ;
            }
            auto const & camera = w.views[0].camera ;
            if( _active_mode == 6 )
            {
                auto const draw_bounds = [&]( node_t const * node, vec4_t const & color )
                {
                    auto const & box = world_bounds( node ) ;
                    if( !valid_bounds( box ) ) return ;
                    bool_t const outside = hit_t::hit_test_frustum_aabb( camera.get_frustum(), box ) == result_t::outside ;
                    w.guide.draw_wire_cube( box.points(), outside ? vec4_t( .32f, .35f, .38f, 1 ) : color ) ;
                } ;
                if( _group_bounds )
                {
                    for( auto * node : w.hierarchy.clusters ) draw_bounds( node, vec4_t( 1, .78f, .25f, 1 ) ) ;
                    for( auto * node : w.hierarchy.rows ) draw_bounds( node, vec4_t( .38f, .65f, .95f, 1 ) ) ;
                }
                if( _root_bounds ) draw_bounds( w.hierarchy.root, vec4_t( .95f, .45f, .65f, 1 ) ) ;
            }
            w.guide.draw_frustum( camera.get_frustum(), vec4_t( .75f, .83f, 1, 1 ) ) ;
            w.guide.draw_wire_sphere( camera.get_position(), .20f, vec4_t( 1, .85f, .4f, 1 ), 16 ) ;
            w.guide.draw_arrow( camera.get_position(), camera.get_direction() * -1.0f, 1.0f, vec4_t( 1, .85f, .4f, 1 ), .2f, .1f ) ;
            w.guide.set_view_proj( w.views[1].camera.mat_view(), w.views[1].camera.mat_proj() ) ;
            w.guide.prepare_for_rendering() ;
        }

    public:
        intersection_app( options const & opts, bool_t * passed ) noexcept : _opts( opts ), _passed( passed ), _demo( opts.demo ), _paused( opts.still )
        { _mode = opts.scene_bounds ? 6 : 0 ; }
        intersection_app( intersection_app const & ) = delete ;
        intersection_app( intersection_app && rhs ) noexcept : intersection_app( rhs._opts, rhs._passed )
        { assert( rhs._windows[0].id == window_id_t( -1 ) && rhs._windows[1].id == window_id_t( -1 ) ) ; }

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ;
                auto const name = "ai_suites.frustum." + motor::to_string( i ) ;
                motor::application::window_info_t info ;
                bool_t const d3d = _opts.d3d || i == 1 ;
                info.window_name = motor::string_t( "motor | frustum intersections | " ) + ( d3d ? "d3d11" : "gl4" ) ;
                info.x = int_t( 40 + i * 80 ) ; info.y = int_t( 40 + i * 60 ) ;
                info.w = 1280 ; info.h = 800 ; _opts.capture.apply( info ) ;
                w.width = uint_t( info.w ) ; w.height = uint_t( info.h ) ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                for( size_t v = 0 ; v < 2 ; ++v )
                {
                    auto const view_name = name + "." + motor::to_string( v ) ;
                    w.views[v].solid.init( view_name + ".solid" ) ; w.views[v].wire.init( view_name + ".wire" ) ;
                }
                w.guide.init( name + ".guide" ) ; make_states( w, name ) ;
                w.id = create_window( info ) ;
                send_window_message( w.id, [vsync = _opts.vsync]( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
                } ) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused ) _time += data.sec_dt * _speed ;
            _active_mode = _opts.smoke ? int_t( _phase % mode_count ) :
                ( _demo && !_opts.scene_bounds ? int_t( _time / 8 ) % mode_count : _mode ) ;
            bool_t const ortho = _opts.smoke ? _phase >= size_t( mode_count ) :
                ( _demo ? int_t( _time / ( 8 * mode_count ) ) % 2 != 0 : _ortho ) ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ; w.prepared = false ;
                if( w.width < 64 || w.height < 64 ) continue ;
                auto const areas = layout( w.width, w.height ) ;
                for( size_t v = 0 ; v < 2 ; ++v ) w.views[v].area = areas[v] ;
                make_camera( w.views[0].camera, areas[0], _time, _yaw,
                    _fov + ( _demo ? .1f * std::sin( _time * .2f ) : 0 ), _far, ortho ) ;
                auto const & frustum = w.views[0].camera.get_frustum() ;
                make_observer( w.views[1].camera, areas[1], frustum ) ;
                if( _active_mode == 6 )
                {
                    w.hierarchy.animate( _time, w.data ) ;
                    w.hierarchy.cull( frustum, w.data ) ;
                }
                else { w.data.animate( _time, _active_mode ) ; w.data.cull( frustum ) ; }
                fill( w ) ;
                if( w.views[0].submitted != w.data.visible || w.views[1].submitted != w.data.visible )
                {
                    motor::log::global_t::test( "[frustum] both views submit the shared visible list", false ) ;
                    _quitting = true ; close() ; return ;
                }
                w.prepared = true ;
            }
        }

        void_t on_first_frame( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = window( id ) ;
            for( auto & v : w.views ) { v.solid.configure( fe ) ; v.wire.configure( fe ) ; }
            w.guide.configure( fe ) ; fe->configure<motor::graphics::state_object_t>( &w.overlay ) ;
        }

        void_t on_render( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = window( id ) ;
            if( _quitting || !w.prepared ) return ;
            if( w.dirty )
            {
                w.clear.access_render_state( 0, [&]( auto & s )
                { s.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, w.width, w.height ) ; return true ; } ) ;
                fe->configure<motor::graphics::state_object_t>( &w.clear ) ;
                for( auto & v : w.views )
                {
                    v.state.access_render_state( 0, [&]( auto & s )
                    { s.view_s.ss.vp = v.area.viewport( w.height ) ; s.scissor_s.ss.rect = s.view_s.ss.vp ; return true ; } ) ;
                    fe->configure<motor::graphics::state_object_t>( &v.state ) ;
                }
                w.dirty = false ;
            }
            for( auto & v : w.views ) { v.solid.prepare_for_rendering( fe ) ; v.wire.prepare_for_rendering( fe ) ; }
            w.guide.prepare_for_rendering( fe ) ;
            fe->push( &w.clear ) ;
            for( size_t index = 0 ; index < 2 ; ++index )
            {
                auto & v = w.views[index] ; fe->push( &v.state ) ;
                v.solid.render( fe ) ; v.wire.render( fe ) ;
                if( index == 1 )
                {
                    fe->push( &w.overlay ) ; w.guide.render( fe ) ;
                    fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
                }
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            fe->fence( [&w]( motor::graphics::gen4::frontend::fence_phase const phase )
            { if( phase == motor::graphics::gen4::frontend::fence_phase::execute ) ++w.completed ; } ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( !_opts.ui || _quitting || _opts.smoke ) return false ;
            auto const & w = window( id ) ; if( !w.prepared || w.width < 480 ) return false ;
            auto * draw = ImGui::GetForegroundDrawList() ;
            draw->AddText( ImVec2( 18, 38 ), IM_COL32( 235, 239, 241, 255 ), modes[_active_mode] ) ;
            for( size_t i = 0 ; i < 2 ; ++i )
            {
                auto const & r = w.views[i].area ;
                ImVec2 const a( float_t( r.x ), float_t( r.y ) ), b( float_t( r.x+r.width ), float_t( r.y+r.height ) ) ;
                draw->AddRect( a, b, IM_COL32( 75, 82, 86, 255 ) ) ;
                draw->AddRectFilled( a, ImVec2( b.x, a.y + 34 ), IM_COL32( 20, 23, 25, 235 ) ) ;
                auto const title = motor::string_t( i == 0 ? "CULLING CAMERA" : "OBSERVER" ) +
                    "  |  " + motor::to_string( w.data.visible.size() ) + " / " + motor::to_string( object_count ) ;
                draw->AddText( ImVec2( a.x + 12, a.y + 10 ), IM_COL32( 235, 239, 241, 255 ), title.c_str() ) ;
            }
            auto const counts = motor::to_string( w.data.inside ) + " inside   " + motor::to_string( w.data.touching ) +
                " intersect   " + motor::to_string( object_count - w.data.visible.size() ) + " outside" ;
            auto const text_size = ImGui::CalcTextSize( counts.c_str() ) ;
            draw->AddText( ImVec2( float_t( w.width ) - text_size.x - 18, 38 ), IM_COL32( 180, 198, 190, 255 ), counts.c_str() ) ;
            if( id == _windows[0].id && !( _demo && _opts.capture.output != sample_support::capture_options::format::original ) )
            {
                ImGui::SetNextWindowPos( ImVec2( 24, 112 ), ImGuiCond_FirstUseEver ) ;
                ImGui::SetNextWindowCollapsed( true, ImGuiCond_Once ) ;
                if( ImGui::Begin( "Intersection controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
                {
                    ImGui::Checkbox( "Demo", &_demo ) ; ImGui::SameLine() ; ImGui::Checkbox( "Pause", &_paused ) ;
                    ImGui::BeginDisabled( _demo ) ;
                    int_t mode = _demo ? _active_mode : _mode ;
                    bool_t ortho = _demo ? w.views[0].camera.is_orthographic() : _ortho ;
                    if( ImGui::Combo( "Test", &mode, modes, mode_count ) ) _mode = mode ;
                    if( ImGui::Checkbox( "Orthographic", &ortho ) ) _ortho = ortho ;
                    ImGui::EndDisabled() ;
                    ImGui::Checkbox( "Bounds", &_bounds ) ;
                    if( _active_mode == 6 )
                    {
                        ImGui::Checkbox( "Group bounds", &_group_bounds ) ;
                        ImGui::Checkbox( "Root bounds", &_root_bounds ) ;
                        ImGui::Text( "Leaf tests: %u / %u", unsigned( w.hierarchy.tested_leaves ), unsigned( object_count ) ) ;
                        ImGui::Text( "Rejected groups: %u", unsigned( w.hierarchy.rejected_groups ) ) ;
                    }
                    ImGui::SliderFloat( "Speed", &_speed, .1f, 2 ) ;
                    ImGui::SliderFloat( "Aim", &_yaw, -1, 1 ) ;
                    ImGui::SliderFloat( "Field of view", &_fov, .45f, 1.25f ) ;
                    ImGui::SliderFloat( "Far plane", &_far, 6, 26 ) ;
                    if( ImGui::Button( "Reset" ) ) { _time = 0 ; _yaw = 0 ; _fov = .82f ; _far = 18 ; }
                }
                ImGui::End() ;
            }
            return true ;
        }

        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.close_changed ) { _quitting = true ; close() ; }
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & w = window( id ) ; w.width = uint_t( event.resize_msg.w ) ; w.height = uint_t( event.resize_msg.h ) ;
                w.dirty = true ; w.prepared = false ;
            }
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < count() ; ++i ) if( _windows[i].completed.load() < 15 ) return ;
            motor::log::global_t::test( "[frustum] submitted both views, phase " + motor::to_string( _phase ), true ) ;
            if( ++_phase == size_t( mode_count * 2 ) ) { *_passed = true ; _quitting = true ; close() ; return ; }
            for( size_t i = 0 ; i < count() ; ++i ) _windows[i].completed.store( 0 ) ;
        }
        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting && std::chrono::steady_clock::now() - _started > std::chrono::seconds( 45 ) )
            {
                motor::log::global_t::test( "[frustum] smoke timeout", false ) ; _quitting = true ; close() ;
            }
        }
        void_t on_last_frame( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = window( id ) ;
            fe->release<motor::graphics::state_object_t>( &w.clear ) ; fe->release<motor::graphics::state_object_t>( &w.overlay ) ;
            for( auto & v : w.views ) fe->release<motor::graphics::state_object_t>( &v.state ) ;
            // Renderer resources remain alive until backend/window teardown.
        }
        void_t on_shutdown( void_t ) noexcept override
        {
            for( size_t i = 0 ; i < count() ; ++i )
            {
                for( auto & v : _windows[i].views ) { v.solid.release() ; v.wire.release() ; }
                _windows[i].guide.release() ;
            }
        }
    } ;
}

int main( int argc, char ** argv )
{
    for( int i = 1 ; i < argc ; ++i )
        if( motor::string_t( argv[i] ) == "--stress" ) return run_culling_stress( argc, argv ) ;
    intersection_sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) { opts.d3d = false ; opts.dual = false ; }
        else if( arg == "--d3d-only" ) { opts.d3d = true ; opts.dual = false ; }
        else if( arg == "--dual" ) { opts.d3d = false ; opts.dual = true ; }
        else if( arg == "--cpu-only" ) opts.cpu = true ;
        else if( arg == "--demo" ) opts.demo = true ;
        else if( arg == "--scene-bounds" ) opts.scene_bounds = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--no-ui" ) opts.ui = false ;
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( arg == "--youtube-short" && i+1 < argc && opts.capture.parse( argv[i+1] ) ) ++i ;
        else
        {
            motor::log::global_t::status( "Usage: 27_frustum_intersections [--gl-only|--d3d-only|--dual] [--scene-bounds] [--demo] [--still] [--no-ui] [--no-vsync] [--youtube-short square|vertical] [--cpu-only|--smoke]" ) ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( !opts.cpu && ( opts.d3d || opts.dual ) )
    {
        motor::log::global_t::error( "d3d11 requires Windows." ) ; motor::log::global_t::deinit() ; return 2 ;
    }
#endif
    bool const cpu_ok = intersection_sample::cpu_checks() ;
    bool passed = false ; int result = 0 ;
    if( cpu_ok && !opts.cpu )
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared( intersection_sample::intersection_app( opts, &passed ) ) ) ;
        result = carrier->exec() ; motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global_t::deinit() ; motor::io::global_t::deinit() ;
    motor::profiling::global_t::deinit() ; motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return result != 0 ? result : ( !cpu_ok || !clean || ( opts.smoke && !opts.cpu && !passed ) ? 1 : 0 ) ;
}
