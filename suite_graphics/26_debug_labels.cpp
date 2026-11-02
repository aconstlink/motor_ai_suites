#include <motor/platform/global.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/gfx/font/text_render_2d.h>
#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/format/global.h>
#include <motor/format/future_items.hpp>
#include <motor/property/property_sheet.hpp>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/io/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/std/string>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>

namespace sample
{
    using namespace motor::core::types ;
    using vec2_t = motor::math::vec2f_t ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using camera_t = motor::gfx::generic_camera_t ;
    using frontend_t = motor::graphics::gen4::frontend_ptr_t ;
    using primitives_t = motor::gfx::primitive_render_3d_t ;

    struct options
    {
        bool_t d3d = false, dual = false, smoke = false, still = false, cpu_only = false ;
    } ;

    // motor's camera projection uses [-w,+w] for clip-space Z on both backends.
    bool_t project_anchor( motor::math::mat4f_cref_t view_projection,
        vec3_t const & world, vec2_t & ndc ) noexcept
    {
        auto const clip = view_projection * vec4_t( world, 1.0f ) ;
        if( !std::isfinite( clip.x() ) || !std::isfinite( clip.y() ) ||
            !std::isfinite( clip.z() ) || !std::isfinite( clip.w() ) || clip.w() <= 1e-6f ) return false ;
        if( clip.x() < -clip.w() || clip.x() > clip.w() ||
            clip.y() < -clip.w() || clip.y() > clip.w() ||
            clip.z() < -clip.w() || clip.z() > clip.w() ) return false ;
        ndc = vec2_t( clip.x() / clip.w(), clip.y() / clip.w() ) ;
        return true ;
    }

    vec2_t pixel_offset( motor::math::vec2ui_cref_t size, vec2_t const & pixels ) noexcept
    {
        return vec2_t( 2.0f * pixels.x() / float_t( size.x() ),
            2.0f * pixels.y() / float_t( size.y() ) ) ;
    }

    bool_t projection_checks( void_t )
    {
        bool_t passed = true ;
        auto check = [&]( char const * name, bool_t const condition )
        {
            motor::log::global_t::test( motor::string_t( "[labels] " ) + name, condition ) ;
            passed = condition && passed ;
        } ;
        auto const identity = motor::math::mat4f_t().identity() ;
        vec2_t point ;
        check( "identity projection", project_anchor( identity, vec3_t( .25f, -.5f, 0 ), point ) &&
            std::abs( point.x() - .25f ) < 1e-5f && std::abs( point.y() + .5f ) < 1e-5f ) ;
        check( "off-screen anchor rejected", !project_anchor( identity, vec3_t( 2, 0, 0 ), point ) ) ;
        check( "non-finite anchor rejected", !project_anchor( identity,
            vec3_t( std::numeric_limits<float_t>::quiet_NaN(), 0, 0 ), point ) ) ;

        for( auto const size : { motor::math::vec2ui_t( 1280, 720 ),
            motor::math::vec2ui_t( 720, 1280 ), motor::math::vec2ui_t( 900, 900 ) } )
        {
            camera_t camera ;
            camera.make_perspective_fov( float_t( size.x() ), float_t( size.y() ), .85f, .1f, 100 ) ;
            camera.look_at( vec3_t( 0, 0, 5 ), vec3_t( 0, 1, 0 ), vec3_t( 0 ) ) ;
            auto const vp = camera.mat_proj() * camera.mat_view() ;
            auto const suffix = " (" + motor::to_string( size.x() ) + "x" + motor::to_string( size.y() ) + ")" ;
            check( ( "target stays centered" + suffix ).c_str(), project_anchor( vp, vec3_t( 0 ), point ) &&
                std::abs( point.x() ) < 1e-5f && std::abs( point.y() ) < 1e-5f ) ;
            check( ( "behind camera rejected" + suffix ).c_str(), !project_anchor( vp, vec3_t( 0, 0, 6 ), point ) ) ;
            check( ( "camera plane rejected" + suffix ).c_str(), !project_anchor( vp, vec3_t( 0, 0, 5 ), point ) ) ;
            check( ( "before near plane rejected" + suffix ).c_str(), !project_anchor( vp, vec3_t( 0, 0, 4.99f ), point ) ) ;
            check( ( "beyond far plane rejected" + suffix ).c_str(), !project_anchor( vp, vec3_t( 0, 0, -110 ), point ) ) ;
            auto const offset = pixel_offset( size, vec2_t( 12, 18 ) ) ;
            check( ( "pixel offsets survive resize" + suffix ).c_str(),
                std::abs( offset.x() * float_t( size.x() ) * .5f - 12 ) < 1e-5f &&
                std::abs( offset.y() * float_t( size.y() ) * .5f - 18 ) < 1e-5f ) ;

            camera.make_orthographic( 8, 6, .1f, 100 ) ;
            auto const ortho = camera.mat_proj() * camera.mat_view() ;
            check( ( "orthographic center and behind-camera rejection" + suffix ).c_str(),
                project_anchor( ortho, vec3_t( 0 ), point ) && std::abs( point.x() ) < 1e-5f &&
                std::abs( point.y() ) < 1e-5f && !project_anchor( ortho, vec3_t( 0, 0, 6 ), point ) ) ;
        }
        return passed ;
    }

    motor::font::glyph_atlas_mtr_t load_font( void_t )
    {
        motor::io::database_t db( motor::io::path_t( MOTOR_FONT_ASSETS ), "./", "debug_labels" ) ;
        motor::property::property_sheet_mtr_t properties = motor::shared( motor::property::property_sheet_t() ) ;
        {
            motor::font::code_points_t points ;
            for( uint32_t c = 33 ; c <= 126 ; ++c ) points.emplace_back( c ) ;
            properties->set_value( "code_points", points ) ;
            properties->set_value<size_t>( "atlas_width", 512 ) ;
            properties->set_value<size_t>( "atlas_height", 512 ) ;
            properties->set_value<size_t>( "point_size", 48 ) ;
            properties->set_value<size_t>( "dpi", 72 ) ;
        }
        auto * registry = motor::format::global::register_default_modules(
            motor::shared( motor::format::module_registry_t() ) ) ;
        auto future = registry->import_from( motor::io::location_t( "Roboto-Medium.ttf" ),
            &db, motor::move( properties ) ) ;
        auto * item = future.get() ;
        auto * atlas_item = dynamic_cast<motor::format::glyph_atlas_item_t *>( item ) ;
        motor::font::glyph_atlas_mtr_t atlas = nullptr ;
        if( atlas_item != nullptr && atlas_item->obj != nullptr && atlas_item->obj->get_num_images() > 0 )
        {
            bool_t valid = true ;
            for( uint32_t c = 33 ; c <= 126 ; ++c )
            {
                size_t index = 0 ;
                motor::font::glyph_atlas_t::glyph_info_t glyph ;
                valid = atlas_item->obj->find_glyph( 0, c, index, glyph ) && valid ;
            }
            if( valid ) atlas = motor::move( atlas_item->obj ) ;
        }
        if( item != nullptr ) { item->release() ; motor::release( motor::move( item ) ) ; }
        motor::release( motor::move( registry ) ) ;
        motor::log::global_t::test( "[labels] imported ASCII font through database/format", atlas != nullptr ) ;
        return atlas ;
    }

    bool_t font_checks( void_t )
    {
        auto * atlas = load_font() ;
        if( atlas == nullptr ) return false ;
        {
            motor::gfx::text_render_2d_t text ;
            text.init( "ai_suites.label_cpu", motor::move( atlas ), 2 ) ;
            auto const identity = motor::math::mat4f_t().identity() ;
            text.set_view_proj( identity, identity ) ;
            for( size_t phase = 0 ; phase < 3 ; ++phase )
            {
                if( phase != 1 )
                {
                    text.draw_text( 0, 0, 28, vec2_t( 0 ), vec4_t( .1f, .1f, .1f, 1 ), "Sphere 01" ) ;
                    text.draw_text( 1, 0, 28, vec2_t( 0 ), vec4_t( 1 ), "Sphere 01" ) ;
                    text.draw_text( 1, 0, 28, vec2_t( 0 ), vec4_t( 1 ), "" ) ;
                }
                text.prepare_for_rendering() ;
            }
        }
        motor::log::global_t::test( "[labels] text populated/empty/restored CPU lifecycle completed", true ) ;
        return true ;
    }

    class label_app : public motor::application::app
    {
        struct window_data
        {
            window_id_t id = window_id_t( -1 ) ;
            motor::math::vec2ui_t size = motor::math::vec2ui_t( 1100, 900 ) ;
            camera_t camera ;
            primitives_t objects, leaders ;
            motor::gfx::text_render_2d_mtr_t text = nullptr ;
            motor::graphics::state_object_t clear, overlay ;
            std::atomic_size_t completed { 0 } ;
            size_t visible_labels = 0 ;
            bool_t dirty = true ;
        } ;

        options _opts ;
        bool_t * _passed ;
        window_data _windows[2] ;
        bool_t _quitting = false, _orbit = false, _animate = true, _labels = true, _empty = false ;
        float_t _time = 0, _yaw = .25f, _distance = 18 ;
        int_t _point_size = 28 ;
        size_t _phase = 0 ;
        std::chrono::steady_clock::time_point _started ;

        size_t count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }

        window_data & window( window_id_t const id ) noexcept
        {
            return _windows[id == _windows[0].id ? 0 : 1] ;
        }

        void_t make_states( window_data & w, motor::string_cref_t name )
        {
            {
                motor::graphics::render_state_sets_t state ;
                state.clear_s.do_change = true ;
                state.clear_s.ss.do_activate = true ;
                state.clear_s.ss.do_color_clear = true ;
                state.clear_s.ss.do_depth_clear = true ;
                state.clear_s.ss.clear_color = vec4_t( .025f, .030f, .035f, 1 ) ;
                state.view_s.do_change = true ;
                state.view_s.ss.do_activate = true ;
                state.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, w.size.x(), w.size.y() ) ;
                state.depth_s.do_change = true ;
                state.depth_s.ss.do_activate = true ;
                state.depth_s.ss.do_depth_write = true ;
                state.polygon_s.do_change = true ;
                state.polygon_s.ss.do_activate = false ;
                state.polygon_s.ss.fm = motor::graphics::fill_mode::fill ;
                state.blend_s.do_change = true ;
                state.blend_s.ss.do_activate = false ;
                w.clear = motor::graphics::state_object_t( name + ".clear" ) ;
                w.clear.add_render_state_set( state ) ;
            }
            {
                motor::graphics::render_state_sets_t state ;
                state.depth_s.do_change = true ;
                state.depth_s.ss.do_activate = false ;
                state.depth_s.ss.do_depth_write = false ;
                state.polygon_s.do_change = true ;
                state.polygon_s.ss.do_activate = false ;
                state.polygon_s.ss.fm = motor::graphics::fill_mode::fill ;
                w.overlay = motor::graphics::state_object_t( name + ".overlay" ) ;
                w.overlay.add_render_state_set( state ) ;
            }
        }

        void_t label( window_data & w, vec3_t const & anchor,
            vec4_t const & color, motor::string_cref_t value )
        {
            if( !_labels || _empty ) return ;
            vec2_t ndc ;
            if( !project_anchor( w.camera.mat_proj() * w.camera.mat_view(), anchor, ndc ) ) return ;
            auto const offset = pixel_offset( w.size, vec2_t( 12, 18 ) ) ;
            auto const start = ndc + offset ;
            auto const shadow = start + pixel_offset( w.size, vec2_t( 1.5f, -1.5f ) ) ;
            auto const tick = pixel_offset( w.size, vec2_t( 3, 3 ) ) ;
            w.leaders.draw_line( vec3_t( ndc.x(), ndc.y(), 0 ), vec3_t( start.x(), start.y(), 0 ), color ) ;
            w.leaders.draw_line( vec3_t( ndc.x() - tick.x(), ndc.y(), 0 ),
                vec3_t( ndc.x() + tick.x(), ndc.y(), 0 ), color ) ;
            w.leaders.draw_line( vec3_t( ndc.x(), ndc.y() - tick.y(), 0 ),
                vec3_t( ndc.x(), ndc.y() + tick.y(), 0 ), color ) ;
            w.text->draw_text( 0, 0, size_t( _point_size ), shadow, vec4_t( .01f, .01f, .01f, 1 ), value ) ;
            w.text->draw_text( 1, 0, size_t( _point_size ), start, color, value ) ;
            ++w.visible_labels ;
        }

        void_t fill_scene( window_data & w )
        {
            auto & r = w.objects ;
            {
                r.draw_lines( 34, []( size_t const i )
                {
                    float_t const p = float_t( i % 17 ) - 8 ;
                    return motor::gfx::line_render_3d_t::draw_line_data_t {
                        { i < 17 ? vec3_t( p, 0, -8 ) : vec3_t( -8, 0, p ),
                          i < 17 ? vec3_t( p, 0, 8 ) : vec3_t( 8, 0, p ) }, vec4_t( .16f, .19f, .21f, 1 ) } ;
                } ) ;
            }
            {
                vec3_t const center( -4, 1.3f + .3f * std::sin( _time ), -2.5f ) ;
                vec4_t const color( 1, .73f, .18f, 1 ) ;
                r.draw_wire_sphere( center, 1, color, 64 ) ;
                label( w, center + vec3_t( 0, 1.1f, 0 ), color, "Sphere 01" ) ;
            }
            {
                vec3_t const center( 0, 1.1f, -2.5f ) ;
                vec4_t const color( .2f, .85f, .7f, 1 ) ;
                vec3_t corners[8] ;
                float_t const c = std::cos( _time * .35f ), s = std::sin( _time * .35f ) ;
                for( size_t i = 0 ; i < 8 ; ++i )
                {
                    float_t const x = ( i & 1 ) != 0 ? .85f : -.85f ;
                    float_t const y = ( i & 2 ) != 0 ? .85f : -.85f ;
                    float_t const z = ( i & 4 ) != 0 ? .85f : -.85f ;
                    corners[i] = center + vec3_t( c * x + s * z, y, -s * x + c * z ) ;
                }
                for( size_t i = 0 ; i < 8 ; ++i )
                    for( size_t bit = 1 ; bit <= 4 ; bit <<= 1 )
                        if( ( i & bit ) == 0 ) r.draw_line( corners[i], corners[i | bit], color ) ;
                label( w, center + vec3_t( 0, 1.1f, 0 ), color, "Box 02" ) ;
            }
            {
                vec3_t const center( 4, 1.3f, -2.5f ) ;
                vec4_t const color( .9f, .4f, .6f, 1 ) ;
                r.draw_wire_circle( motor::math::mat3f_t::make_identity(), center, 1.1f, color, 64 ) ;
                label( w, center + vec3_t( 0, 1.25f, 0 ), color, "Ring 03" ) ;
            }
            {
                vec3_t const center( -4, 1.1f, 2.8f ) ;
                vec4_t const color( .35f, .7f, 1, 1 ) ;
                camera_t camera ;
                camera.make_perspective_fov( 1.4f, 1, .65f, .2f, 2.2f ) ;
                camera.look_at( center, vec3_t( 0, 1, 0 ), center + vec3_t( .4f, .15f, -1 ) ) ;
                r.draw_frustum( camera, color ) ;
                label( w, center + vec3_t( 0, 1, 0 ), color, "Camera 04" ) ;
            }
            {
                vec3_t const center( 0, 2.5f, 2.5f ) ;
                vec4_t const color( 1, .5f, .25f, 1 ) ;
                auto const direction = vec3_t( .25f * std::sin( _time * .6f ), -1, -.35f ) ;
                for( int_t i = -1 ; i <= 1 ; ++i )
                    r.draw_arrow( center + vec3_t( float_t( i ) * .5f, 0, 0 ), direction, 2, color ) ;
                label( w, center + vec3_t( 0, .3f, 0 ), color, "Sun 05" ) ;
            }
            {
                vec3_t const center( 3.6f, .2f, 2.8f ) ;
                r.draw_arrow( center, vec3_t( 1, 0, 0 ), 1.6f, vec4_t( 1, .3f, .35f, 1 ) ) ;
                r.draw_arrow( center, vec3_t( 0, 1, 0 ), 1.6f, vec4_t( .4f, 1, .5f, 1 ) ) ;
                r.draw_arrow( center, vec3_t( 0, 0, 1 ), 1.6f, vec4_t( .4f, .6f, 1, 1 ) ) ;
                label( w, center + vec3_t( 0, 2, 0 ), vec4_t( .9f, .94f, .98f, 1 ), "Axes 06" ) ;
            }
        }

    public:
        label_app( options const opts, bool_t * passed ) noexcept : _opts( opts ), _passed( passed ) {}
        label_app( label_app const & ) = delete ;
        label_app( label_app && rhs ) noexcept : label_app( rhs._opts, rhs._passed )
        {
            assert( rhs._windows[0].id == window_id_t( -1 ) && rhs._windows[1].id == window_id_t( -1 ) ) ;
        }

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            auto * atlas = load_font() ;
            if( atlas == nullptr ) { _quitting = true ; close() ; return ; }
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ;
                auto const name = "ai_suites.debug_labels." + motor::to_string( i ) ;
                w.objects.init( name + ".objects" ) ;
                w.leaders.init( name + ".leaders" ) ;
                w.text = motor::shared( motor::gfx::text_render_2d_t() ) ;
                w.text->init( name + ".text", motor::share( atlas ), 2 ) ;
                make_states( w, name ) ;
                motor::application::window_info_t info ;
                bool_t const d3d = _opts.d3d || i == 1 ;
                info.window_name = d3d ? "motor | debug labels | d3d11" : "motor | debug labels | gl4" ;
                info.x = int_t( 40 + i * 80 ) ; info.y = int_t( 40 + i * 60 ) ;
                info.w = w.size.x() ; info.h = w.size.y() ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                    motor::application::graphics_generation::gen4_gl4 ;
                w.id = create_window( info ) ;
                send_window_message( w.id, []( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { true } ) ) ;
                } ) ;
            }
            motor::release( motor::move( atlas ) ) ;
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( _animate && !_opts.still ) _time += data.sec_dt ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ;
                float_t const yaw = _yaw + ( _orbit && !_opts.still ? _time * .15f : 0 ) ;
                w.camera.make_perspective_fov( float_t( w.size.x() ), float_t( w.size.y() ), .85f, .1f, 150 ) ;
                w.camera.look_at( vec3_t( std::sin( yaw ) * _distance, _distance * .6f,
                    std::cos( yaw ) * _distance ), vec3_t( 0, 1, 0 ), vec3_t( 0, 1, 0 ) ) ;
                w.objects.set_view_proj( w.camera.mat_view(), w.camera.mat_proj() ) ;
                auto const identity = motor::math::mat4f_t().identity() ;
                w.leaders.set_view_proj( identity, identity ) ;
                w.text->set_view_proj( identity, identity ) ;
                w.visible_labels = 0 ;
                fill_scene( w ) ;
                w.objects.prepare_for_rendering() ;
                w.leaders.prepare_for_rendering() ;
                // Always prepare, including an empty label frame, to clear stale glyphs.
                w.text->prepare_for_rendering() ;
            }
        }

        void_t on_first_frame( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = window( id ) ;
            w.objects.configure( fe ) ; w.leaders.configure( fe ) ; w.text->configure( fe ) ;
            fe->configure<motor::graphics::state_object_t>( &w.overlay ) ;
        }

        void_t on_render( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            if( _quitting ) return ;
            auto & w = window( id ) ;
            if( w.dirty )
            {
                w.clear.access_render_state( 0, [&]( auto & state )
                {
                    state.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, w.size.x(), w.size.y() ) ;
                    return true ;
                } ) ;
                fe->configure<motor::graphics::state_object_t>( &w.clear ) ;
                w.dirty = false ;
            }
            w.objects.prepare_for_rendering( fe ) ;
            w.leaders.prepare_for_rendering( fe ) ;
            w.text->prepare_for_rendering( fe ) ;
            fe->push( &w.clear ) ;
            w.objects.render( fe ) ;
            fe->push( &w.overlay ) ;
            if( w.visible_labels != 0 ) w.leaders.render( fe ) ;
            w.text->render( fe, 0 ) ;
            w.text->render( fe, 1 ) ;
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            fe->fence( [&w]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase == motor::graphics::gen4::frontend::fence_phase::execute ) ++w.completed ;
            } ) ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < count() ; ++i )
                if( _windows[i].completed.load() < 45 ) return ;
            motor::log::global_t::test( "[labels] executed lifecycle phase " + motor::to_string( _phase ), true ) ;
            if( ++_phase == 3 ) { *_passed = true ; _quitting = true ; close() ; return ; }
            _empty = _phase == 1 ;
            for( size_t i = 0 ; i < count() ; ++i ) _windows[i].completed.store( 0 ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            if( ImGui::Begin( "Debug labels", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::BeginDisabled( _opts.smoke ) ;
                ImGui::Checkbox( "Labels", &_labels ) ;
                ImGui::Checkbox( "Empty text", &_empty ) ;
                ImGui::Checkbox( "Animate", &_animate ) ;
                ImGui::Checkbox( "Orbit", &_orbit ) ;
                ImGui::SliderInt( "Point size", &_point_size, 16, 44 ) ;
                ImGui::SliderFloat( "Yaw", &_yaw, -3.14f, 3.14f ) ;
                ImGui::SliderFloat( "Distance", &_distance, 6, 30 ) ;
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & w = window( id ) ;
                w.size = motor::math::vec2ui_t( uint_t( event.resize_msg.w ), uint_t( event.resize_msg.h ) ) ;
                w.dirty = true ;
            }
            if( event.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting && std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                motor::log::global_t::test( "[labels] smoke timeout", false ) ;
                _quitting = true ; close() ;
            }
        }

        void_t on_last_frame( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = window( id ) ;
            fe->release<motor::graphics::state_object_t>( &w.clear ) ;
            fe->release<motor::graphics::state_object_t>( &w.overlay ) ;
            // Keep renderer-owned resources alive until backend/window teardown.
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            if( !_opts.smoke && _windows[0].text != nullptr ) *_passed = true ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                _windows[i].objects.release() ;
                _windows[i].leaders.release() ;
                motor::release( motor::move( _windows[i].text ) ) ;
            }
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--gl-only" ) { opts.d3d = false ; opts.dual = false ; }
        else if( arg == "--d3d-only" ) { opts.d3d = true ; opts.dual = false ; }
        else if( arg == "--dual" ) { opts.d3d = false ; opts.dual = true ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--cpu-only" ) opts.cpu_only = true ;
        else
        {
            motor::log::global_t::status( "Usage: 26_debug_labels [--gl-only|--d3d-only|--dual] [--still] [--smoke] [--cpu-only]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( ( opts.d3d || opts.dual ) && !opts.cpu_only )
    {
        motor::log::global_t::error( "d3d11 requires Windows." ) ;
        motor::log::global_t::deinit() ;
        return 2 ;
    }
#endif
    bool passed = sample::projection_checks() ;
    int result = 0 ;
    if( passed && opts.cpu_only ) passed = sample::font_checks() ;
    else if( passed )
    {
        passed = false ;
        motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::label_app( opts, &passed ) ) ) ;
        result = carrier->exec() ;
        motor::release( motor::move( carrier ) ) ;
    }
    motor::log::global_t::test( "[labels] checks completed; GPU pixels require visual verification", passed && result == 0 ) ;
    motor::concurrent::global_t::deinit() ;
    motor::io::global_t::deinit() ;
    motor::profiling::global_t::deinit() ;
    motor::log::global_t::deinit() ;
    auto const leaked = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : ( passed && leaked == 0 ? 0 : 1 ) ;
}
