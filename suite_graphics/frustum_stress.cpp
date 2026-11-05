#include "frustum_stress.hpp"
#include "capture_options.hpp"
#include <motor/platform/global.h>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/io/global.h>
#include <motor/profiling/global.h>
#include <charconv>
#include <atomic>

namespace culling_stress
{
    using frontend_t = motor::graphics::gen4::frontend_ptr_t ;
    using primitive_t = motor::gfx::primitive_render_3d_t ;
    struct options
    {
        sample_support::capture_options capture ;
        size_t objects = 10000, animated = 0 ;
        mode method = mode::hierarchy ;
        bool_t d3d = false, dual = false, cpu = false, demo = false, ui = true, vsync = true, smoke = false ;
    } ;

    class stress_app : public motor::application::app
    {
        struct draw_slot
        {
            size_t id ;
            motor::graphics::data_variable<mat4_t> * mvp ;
            motor::graphics::data_variable<vec4_t> * color ;
        } ;
        struct view
        {
            camera_t camera ; rectangle area ;
            motor::graphics::state_object_t state ;
            motor::vector<draw_slot> slots ;
            primitive_t bounds ;
        } ;
        struct window
        {
            window_id_t id = window_id_t( -1 ) ;
            uint_t width = 1280, height = 800 ;
            view views[2] ;
            primitive_t guide ;
            motor::graphics::state_object_t clear, overlay ;
            motor::graphics::msl_object_mtr_t shader = nullptr ;
            motor::vector<size_t> visible ;
            counts stats ;
            double_t cull_ms = 0, variables_ms = 0, commands_ms = 0, debug_ms = 0 ;
            size_t draws = 0 ;
            bool_t dirty = true, prepared = false, ready = false ;
            std::atomic_size_t completed { 0 } ;
        } ;
        options _opts ; field _field ; window _windows[2] ;
        motor::graphics::geometry_object_t _geometry ;
        bool_t * _passed ;
        bool_t _closing = false, _pause = false, _groups = false, _leaves = false, _rebuild = false ;
        int_t _selection = 0, _mode = 2, _animated = 0 ;
        int_t _group_selection = 0, _leaf_selection = 0 ;
        float_t _time = 0, _far = 55, _fov = 1.05f ;
        double_t _bounds_ms = 0, _build_ms = 0 ;
        clock_t::time_point _started, _last_graphics ;
        motor::vector<double_t> _intervals ; size_t _sample = 0, _phase = 0 ;

        size_t window_count( void_t ) const { return _opts.dual ? 2 : 1 ; }
        window & get_window( window_id_t const id ) { return _windows[id == _windows[0].id ? 0 : 1] ; }
        void_t build( void_t )
        {
            auto const start = clock_t::now() ; _field.build( _opts.objects ) ; _build_ms = elapsed( start ) ;
            for( size_t i = 0 ; i < window_count() ; ++i ) _windows[i].visible.reserve( _opts.objects ) ;
            _intervals.clear() ; _sample = 0 ; _last_graphics = clock_t::time_point() ; _rebuild = false ;
            motor::log::global_t::status( "[culling stress] built " + motor::to_string( _opts.objects ) +
                " scene leaves in " + motor::to_string( _build_ms ) + " ms" ) ;
        }
        void_t geometry( void_t )
        {
            struct vertex { vec3_t position, normal ; } ;
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element( motor::graphics::vertex_attribute::position, motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .add_layout_element( motor::graphics::vertex_attribute::normal, motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .resize( 24 ).update<vertex>( []( vertex * v, size_t )
                {
                    vec3_t const p[] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1}, {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} } ;
                    size_t const faces[6][4] = { {4,5,6,7},{1,0,3,2},{0,4,7,3},{5,1,2,6},{3,7,6,2},{0,1,5,4} } ;
                    vec3_t const n[] = { {0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0} } ;
                    for( size_t f = 0 ; f < 6 ; ++f ) for( size_t c = 0 ; c < 4 ; ++c )
                        v[f * 4 + c] = { p[faces[f][c]] * .55f, n[f] } ;
                } ) ;
            auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint )
                .resize( 36 ).update<uint_t>( []( uint_t * p, size_t )
                {
                    uint_t const pattern[] = { 0,1,2,0,2,3 } ;
                    for( uint_t f = 0 ; f < 6 ; ++f ) for( uint_t c = 0 ; c < 6 ; ++c ) p[f * 6 + c] = f * 4 + pattern[c] ;
                } ) ;
            _geometry = motor::graphics::geometry_object_t( "culling_stress.cube", motor::graphics::primitive_type::triangles,
                std::move( vb ), std::move( ib ) ) ;
        }
        void_t shader( window & w, motor::string_cref_t name )
        {
            w.shader = motor::shared( motor::graphics::msl_object_t( name, true ) ) ;
            w.shader->add( motor::graphics::msl_api_type::msl_4_0, R"(
                config culling_stress
                {
                    vertex_shader
                    {
                        in vec3_t pos : position ;
                        in vec3_t nrm : normal ;
                        out vec4_t pos : position ;
                        out vec3_t nrm : texcoord0 ;
                        mat4_t mvp ;
                        void main() { out.pos = mvp * vec4_t( in.pos, 1.0 ) ; out.nrm = in.nrm ; }
                    }
                    pixel_shader
                    {
                        in vec3_t nrm : texcoord0 ;
                        out vec4_t color : color ;
                        vec4_t base_color ;
                        void main()
                        {
                            float_t light = 0.3 + 0.7 * max( dot( normalize( in.nrm ), normalize( vec3_t( 0.4, 0.8, 0.6 ) ) ), 0.0 ) ;
                            out.color = vec4_t( base_color.xyz * light, 1.0 ) ;
                        }
                    }
                }
            )" ) ;
            w.shader->link_geometry( "culling_stress.cube" ) ;
            for( auto & v : w.views ) ensure_slots( w, v, 1 ) ;
        }
        void_t ensure_slots( window & w, view & v, size_t const count )
        {
            while( v.slots.size() < count )
            {
                auto vs = motor::shared( motor::graphics::variable_set_t() ) ;
                auto * mvp = vs->data_variable<mat4_t>( "mvp" ) ; mvp->set( mat4_t::make_identity() ) ;
                auto * color = vs->data_variable<vec4_t>( "base_color" ) ; color->set( vec4_t( 1 ) ) ;
                auto const id = w.shader->add_variable_set( motor::move( vs ) ) ;
                v.slots.emplace_back( draw_slot{ id, mvp, color } ) ;
            }
        }
        void_t states( window & w, motor::string_cref_t name )
        {
            motor::graphics::render_state_sets_t s ;
            s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ;
            s.clear_s.ss.do_color_clear = true ; s.clear_s.ss.do_depth_clear = true ;
            s.clear_s.ss.clear_color = vec4_t( .024f, .03f, .037f, 1 ) ;
            s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = true ;
            s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
            s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
            s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
            s.scissor_s.do_change = true ; s.scissor_s.ss.do_activate = false ;
            s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
            s.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, w.width, w.height ) ;
            w.clear = motor::graphics::state_object_t( name + ".clear" ) ; w.clear.add_render_state_set( s ) ;
            for( size_t i = 0 ; i < 2 ; ++i )
            {
                motor::graphics::render_state_sets_t vp ;
                vp.view_s.do_change = true ; vp.view_s.ss.do_activate = true ;
                vp.scissor_s.do_change = true ; vp.scissor_s.ss.do_activate = true ;
                w.views[i].state = motor::graphics::state_object_t( name + ".view." + motor::to_string( i ) ) ;
                w.views[i].state.add_render_state_set( vp ) ;
            }
            motor::graphics::render_state_sets_t overlay ;
            overlay.depth_s.do_change = true ; overlay.depth_s.ss.do_activate = false ; overlay.depth_s.ss.do_depth_write = false ;
            w.overlay = motor::graphics::state_object_t( name + ".overlay" ) ; w.overlay.add_render_state_set( overlay ) ;
        }
        void_t prepare_debug( window & w )
        {
            auto const start = clock_t::now() ;
            size_t const leaf_count = debug_bounds_count( w.visible.size(), debug_leaf_limit, _leaf_selection == 1 ) ;
            for( auto & v : w.views )
            {
                if( _leaves ) for( size_t sample = 0 ; sample < leaf_count ; ++sample )
                {
                    size_t const i = debug_bounds_index( sample, w.visible.size(), leaf_count ) ;
                    v.bounds.draw_wire_cube( _field.items[w.visible[i]].bounds->get_coarse_transformed().points(), vec4_t( .8f, .9f, .95f, 1 ) ) ;
                }
                v.bounds.set_view_proj( v.camera.mat_view(), v.camera.mat_proj() ) ; v.bounds.prepare_for_rendering() ;
            }
            auto const & frustum = w.views[0].camera.get_frustum() ;
            size_t const group_count = debug_bounds_count( _field.chunks.size(), debug_group_limit, _group_selection == 1 ) ;
            if( _groups ) for( size_t sample = 0 ; sample < group_count ; ++sample )
            {
                size_t const c = debug_bounds_index( sample, _field.chunks.size(), group_count ) ;
                auto const & b = world_bounds( _field.chunks[c].node ) ;
                bool_t const outside = hit_t::hit_test_frustum_aabb( frustum, b ) == result_t::outside ;
                w.guide.draw_wire_cube( b.points(), outside ? vec4_t( .22f, .24f, .27f, 1 ) : vec4_t( .9f, .72f, .3f, 1 ) ) ;
            }
            w.guide.draw_frustum( frustum, vec4_t( .87f, .9f, 1, 1 ) ) ;
            w.guide.draw_wire_cube( _field.bounds().points(), vec4_t( .27f, .32f, .38f, 1 ) ) ;
            w.guide.set_view_proj( w.views[1].camera.mat_view(), w.views[1].camera.mat_proj() ) ;
            w.guide.prepare_for_rendering() ; w.debug_ms = elapsed( start ) ;
        }
    public:
        stress_app( options const & opts, bool_t * passed ) : _opts( opts ), _passed( passed ),
            _mode( int_t( opts.method ) ), _animated( int_t( opts.animated ) )
        { _selection = opts.objects == 50000 ? 1 : opts.objects == 100000 ? 2 : opts.objects == 200000 ? 3 : 0 ; }
        stress_app( stress_app const & ) = delete ;
        stress_app( stress_app && rhs ) noexcept : stress_app( rhs._opts, rhs._passed ) {}

        void_t on_init( void_t ) noexcept override
        {
            geometry() ; build() ; _intervals.reserve( 240 ) ; _started = clock_t::now() ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                auto & w = _windows[i] ; auto const name = "culling_stress." + motor::to_string( i ) ;
                motor::application::window_info_t info ;
                bool_t const d3d = _opts.d3d || i == 1 ;
                info.window_name = motor::string_t( "motor | culling stress | " ) + ( d3d ? "d3d11" : "gl4" ) ;
                info.x = int_t( 40 + i * 80 ) ; info.y = int_t( 40 + i * 60 ) ; info.w = 1280 ; info.h = 800 ;
                _opts.capture.apply( info ) ; w.width = uint_t( info.w ) ; w.height = uint_t( info.h ) ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                states( w, name ) ; shader( w, name ) ;
                for( size_t v = 0 ; v < 2 ; ++v ) w.views[v].bounds.init( name + ".bounds." + motor::to_string( v ) ) ;
                w.guide.init( name + ".guide" ) ; w.id = create_window( info ) ;
                send_window_message( w.id, [vsync = _opts.vsync]( auto & wnd )
                {
                    wnd.send_message( motor::application::show_message( { true } ) ) ;
                    wnd.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
                } ) ;
            }
        }
        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _closing ) return ;
            if( _rebuild ) build() ;
            auto const now = clock_t::now() ;
            if( _last_graphics != clock_t::time_point() )
            {
                double_t const dt = std::chrono::duration<double_t, std::milli>( now - _last_graphics ).count() ;
                if( _intervals.size() < 240 ) _intervals.emplace_back( dt ) ; else _intervals[_sample++ % 240] = dt ;
            }
            _last_graphics = now ;
            if( !_pause ) _time += data.sec_dt ;
            auto start = clock_t::now() ;
            bool_t const changed = _field.animate( _time, size_t( _animated ) ) ;
            _bounds_ms = changed ? elapsed( start ) : 0 ;
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                auto & w = _windows[i] ; w.prepared = false ;
                if( w.width < 64 || w.height < 64 ) continue ;
                auto const areas = layout( w.width, w.height ) ;
                for( size_t v = 0 ; v < 2 ; ++v ) w.views[v].area = areas[v] ;
                camera_for( w.views[0].camera, areas[0], _field.bounds(), _time, _far, _fov ) ;
                observer_for( w.views[1].camera, areas[1], _field.bounds(), w.views[0].camera.get_frustum() ) ;
                start = clock_t::now() ;
                w.stats = _field.cull( w.views[0].camera.get_frustum(), mode( _mode ), w.visible ) ;
                w.cull_ms = elapsed( start ) ;
                start = clock_t::now() ;
                for( auto & v : w.views )
                {
                    ensure_slots( w, v, w.visible.size() ) ;
                    auto const vp = v.camera.mat_proj() * v.camera.mat_view() ;
                    for( size_t j = 0 ; j < w.visible.size() ; ++j )
                    {
                        auto const & obj = _field.items[w.visible[j]] ;
                        v.slots[j].mvp->set( vp * obj.transform->get_computed().get_transformation() ) ;
                        v.slots[j].color->set( vec4_t( obj.color, 1 ) ) ;
                    }
                }
                w.variables_ms = elapsed( start ) ; prepare_debug( w ) ; w.prepared = true ;
            }
        }
        void_t on_first_frame( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = get_window( id ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->configure<motor::graphics::msl_object_t>( w.shader ) ;
            fe->configure<motor::graphics::state_object_t>( &w.overlay ) ;
            for( auto & v : w.views ) v.bounds.configure( fe ) ; w.guide.configure( fe ) ;
        }
        void_t on_render( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = get_window( id ) ; w.draws = 0 ; w.commands_ms = 0 ;
            if( _closing || !w.prepared ) return ;
            auto const status = fe->decode( w.shader ) ;
            w.ready = status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
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
            for( auto & v : w.views ) v.bounds.prepare_for_rendering( fe ) ; w.guide.prepare_for_rendering( fe ) ;
            auto const start = clock_t::now() ; fe->push( &w.clear ) ;
            for( size_t v = 0 ; v < 2 ; ++v )
            {
                auto & view = w.views[v] ; fe->push( &view.state ) ;
                if( w.ready ) for( size_t j = 0 ; j < w.visible.size() ; ++j )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.geo = 0 ; detail.varset = view.slots[j].id ;
                    fe->render( w.shader, detail ) ; ++w.draws ;
                }
                view.bounds.render( fe ) ;
                if( v == 1 )
                {
                    fe->push( &w.overlay ) ; w.guide.render( fe ) ;
                    fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
                }
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ; w.commands_ms = elapsed( start ) ;
            if( w.ready ) fe->fence( [&w]( motor::graphics::gen4::frontend::fence_phase const phase )
            { if( phase == motor::graphics::gen4::frontend::fence_phase::execute ) ++w.completed ; } ) ;
        }
        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( !_opts.ui || _closing ) return false ;
            auto const & w = get_window( id ) ; if( !w.prepared ) return false ;
            auto * draw = ImGui::GetForegroundDrawList() ;
            for( size_t v = 0 ; v < 2 ; ++v )
            {
                auto const & r = w.views[v].area ;
                auto const title = motor::string_t( v == 0 ? "CULLING CAMERA" : "OBSERVER" ) + "  |  " +
                    motor::to_string( w.visible.size() ) + " / " + motor::to_string( _field.items.size() ) ;
                draw->AddText( ImVec2( float_t( r.x + 12 ), float_t( r.y + 12 ) ), IM_COL32( 240,240,240,255 ), title.c_str() ) ;
            }
            auto const summary = motor::string_t( names[_mode] ) + " | cube draws (both views): " + motor::to_string( w.draws ) ;
            draw->AddText( ImVec2( 18, 38 ), IM_COL32( 220,230,235,255 ), summary.c_str() ) ;
            if( _opts.demo && _opts.capture.output != sample_support::capture_options::format::original ) return true ;
            ImGui::SetNextWindowPos( ImVec2( 24, 115 ), ImGuiCond_FirstUseEver ) ;
            ImGui::SetNextWindowCollapsed( true, ImGuiCond_Once ) ;
            if( ImGui::Begin( "Culling stress", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                char const * amounts[] = { "10,000", "50,000", "100,000", "200,000" } ;
                if( ImGui::Combo( "Objects", &_selection, amounts, 4 ) )
                { size_t const n[] = { 10000, 50000, 100000, 200000 } ; _opts.objects = n[_selection] ; _rebuild = true ; }
                if( ImGui::Combo( "Culling", &_mode, names, 3 ) ) { _intervals.clear() ; _sample = 0 ; }
                ImGui::Checkbox( "Pause camera", &_pause ) ;
                ImGui::SliderInt( "Animated objects (%)", &_animated, 0, 100 ) ;
                ImGui::SliderFloat( "Far plane", &_far, 10, 150 ) ;
                ImGui::SliderFloat( "Field of view", &_fov, .45f, 1.4f ) ;
                ImGui::Checkbox( "Group bounds", &_groups ) ;
                if( _groups )
                {
                    char const * selections[] = { "Limited (256)", "All" } ;
                    ImGui::Combo( "Group selection", &_group_selection, selections, 2 ) ;
                    ImGui::Text( "Chunk bounds shown / total: %zu / %zu",
                        debug_bounds_count( _field.chunks.size(), debug_group_limit, _group_selection == 1 ), _field.chunks.size() ) ;
                }
                ImGui::Checkbox( "Leaf bounds", &_leaves ) ;
                if( _leaves )
                {
                    char const * selections[] = { "Limited (128)", "All visible" } ;
                    ImGui::Combo( "Leaf selection", &_leaf_selection, selections, 2 ) ;
                    ImGui::Text( "Leaf bounds shown / visible per view: %zu / %zu",
                        debug_bounds_count( w.visible.size(), debug_leaf_limit, _leaf_selection == 1 ), w.visible.size() ) ;
                }
                ImGui::Text( "Groups tested: %zu | rejected: %zu | inside: %zu", w.stats.group_tests, w.stats.rejected_groups, w.stats.accepted_groups ) ;
                ImGui::Text( "Leaf tests: %zu | submitted per view: %zu", w.stats.leaf_tests, w.visible.size() ) ;
                ImGui::Text( "Cube draws (both views, excluding debug/UI): %zu", w.draws ) ;
                ImGui::Text( "Build: %.2f ms | transforms + bounds: %.3f ms", _build_ms, _bounds_ms ) ;
                ImGui::Text( "Culling: %.3f ms | draw variables: %.3f ms", w.cull_ms, w.variables_ms ) ;
                ImGui::Text( "Debug geometry: %.3f ms | command recording: %.3f ms", w.debug_ms, w.commands_ms ) ;
                if( !_intervals.empty() )
                {
                    auto sorted = _intervals ; std::sort( sorted.begin(), sorted.end() ) ;
                    size_t const n = sorted.size() ;
                    double_t const median = ( sorted[( n - 1 ) / 2] + sorted[n / 2] ) * .5 ;
                    ImGui::Text( "Graphics interval median / p95: %.2f / %.2f ms", median, sorted[size_t( std::ceil( double_t( n ) * .95 ) ) - 1] ) ;
                }
                ImGui::Text( "Shader: %s", w.ready ? "ready" : "pending / failed" ) ;
            }
            ImGui::End() ; return true ;
        }
        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.close_changed ) { _closing = true ; close() ; }
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & w = get_window( id ) ; w.width = uint_t( event.resize_msg.w ) ; w.height = uint_t( event.resize_msg.h ) ;
                w.dirty = true ; w.prepared = false ;
            }
        }
        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _closing ) return ;
            for( size_t i = 0 ; i < window_count() ; ++i ) if( _windows[i].completed.load() < 8 ) return ;
            motor::log::global_t::test( "[culling stress] ready shader and execute fences for mode " + motor::to_string( _mode ), true ) ;
            if( ++_phase == 3 ) { *_passed = true ; _closing = true ; close() ; return ; }
            _mode = int_t( _phase ) ;
            for( size_t i = 0 ; i < window_count() ; ++i ) _windows[i].completed.store( 0 ) ;
        }
        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_closing && clock_t::now() - _started > std::chrono::seconds( 90 ) )
            { motor::log::global_t::test( "[culling stress] smoke timeout", false ) ; _closing = true ; close() ; }
        }
        void_t on_last_frame( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = get_window( id ) ; fe->release<motor::graphics::msl_object_t>( w.shader ) ;
            fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->release<motor::graphics::state_object_t>( &w.clear ) ; fe->release<motor::graphics::state_object_t>( &w.overlay ) ;
            for( auto & v : w.views ) fe->release<motor::graphics::state_object_t>( &v.state ) ;
        }
        void_t on_shutdown( void_t ) noexcept override
        {
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                auto & w = _windows[i] ;
                for( auto & v : w.views ) { v.bounds.release() ; v.slots.clear() ; }
                w.guide.release() ; motor::release( motor::move( w.shader ) ) ; w.shader = nullptr ;
            }
        }
    } ;
}

int run_culling_stress( int argc, char ** argv )
{
    using namespace culling_stress ;
    options opts ; bool_t objects_set = false ;
    auto const usage = [] ( void_t )
    {
        motor::log::global_t::status( "27_frustum_intersections --stress [--objects 1..200000] [--culling none|flat|hierarchy] [--animate-percent 0..100] [--gl-only|--d3d-only|--dual] [--demo] [--no-ui] [--no-vsync] [--youtube-short square|vertical] [--cpu-only|--smoke]" ) ;
    } ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ; bool_t valid = true ;
        if( arg == "--stress" ) continue ;
        else if( arg == "--help" ) { usage() ; motor::log::global_t::deinit() ; return 0 ; }
        else if( arg == "--gl-only" ) { opts.d3d = false ; opts.dual = false ; }
        else if( arg == "--d3d-only" ) { opts.d3d = true ; opts.dual = false ; }
        else if( arg == "--dual" ) { opts.d3d = false ; opts.dual = true ; }
        else if( arg == "--demo" ) opts.demo = true ;
        else if( arg == "--no-ui" ) opts.ui = false ;
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( arg == "--cpu-only" ) opts.cpu = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--youtube-short" && i + 1 < argc ) valid = opts.capture.parse( argv[++i] ) ;
        else if( ( arg == "--objects" || arg == "--animate-percent" ) && i + 1 < argc )
        {
            motor::string_t const value( argv[++i] ) ; size_t n = 0 ;
            auto const parsed = std::from_chars( value.data(), value.data() + value.size(), n ) ;
            valid = parsed.ec == std::errc() && parsed.ptr == value.data() + value.size() ;
            if( arg == "--objects" ) { valid = valid && n >= 1 && n <= 200000 ; opts.objects = n ; objects_set = true ; }
            else { valid = valid && n <= 100 ; opts.animated = n ; }
        }
        else if( arg == "--culling" && i + 1 < argc )
        {
            motor::string_t const m( argv[++i] ) ;
            if( m == "none" ) opts.method = mode::none ; else if( m == "flat" ) opts.method = mode::flat ;
            else if( m == "hierarchy" ) opts.method = mode::hierarchy ; else valid = false ;
        }
        else valid = false ;
        if( !valid ) { usage() ; motor::log::global_t::deinit() ; return 2 ; }
    }
#ifndef _WIN32
    if( !opts.cpu && ( opts.d3d || opts.dual ) )
    { motor::log::global_t::error( "d3d11 requires Windows." ) ; motor::log::global_t::deinit() ; return 2 ; }
#endif
    if( opts.smoke ) { if( !objects_set ) opts.objects = 1025 ; opts.method = mode::none ; }
    bool_t ok = true, passed = false ; int result = 0 ;
    if( opts.cpu ) ok = checks( objects_set ? opts.objects : 4096 ) ;
    else
    {
        auto carrier = motor::platform::global_t::create_carrier( motor::shared( stress_app( opts, &passed ) ) ) ;
        result = carrier->exec() ; motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global_t::deinit() ; motor::io::global_t::deinit() ;
    motor::profiling::global_t::deinit() ; motor::log::global_t::deinit() ;
    bool_t const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return result != 0 ? result : ( !ok || !clean || ( opts.smoke && !opts.cpu && !passed ) ? 1 : 0 ) ;
}
