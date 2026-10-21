#include <motor/platform/global.h>
#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/gfx/primitive/tri_render_3d.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using frontend_t = motor::graphics::gen4::frontend_ptr_t ;
    using primitives_t = motor::gfx::primitive_render_3d_t ;
    using camera_t = motor::gfx::generic_camera_t ;

    struct options
    {
        bool_t dual = false, d3d = false, smoke = false, still = false ;
    } ;

    // Validate the camera's half-dimensions against its actual projection.
    bool_t validate_frustum( camera_t const & camera )
    {
        auto const nf = camera.get_near_far() ;
        auto const dims = camera.near_far_plane_half_dims() ;
        auto const vp = camera.mat_proj() * camera.mat_view() ;
        bool_t valid = true ;
        float_t const sx[4] = {-1,-1,1,1}, sy[4] = {-1,1,1,-1} ;
        for( size_t plane = 0 ; plane < 2 ; ++plane )
        {
            float_t const depth = plane == 0 ? nf.x() : nf.y() ;
            float_t const half_width = plane == 0 ? dims.x() : dims.z() ;
            float_t const half_height = plane == 0 ? dims.y() : dims.w() ;
            for( size_t i = 0 ; i < 4 ; ++i )
            {
                auto const world = camera.mat_cam() * vec4_t(
                    sx[i]*half_width, sy[i]*half_height, -depth, 1 ) ;
                auto const clip = vp * world ;
                valid &= std::isfinite(clip.w()) && std::abs(clip.w()) > 1e-6f ;
                if( std::abs(clip.w()) > 1e-6f )
                {
                    valid &= std::abs(clip.x()/clip.w()-sx[i]) < .002f ;
                    valid &= std::abs(clip.y()/clip.w()-sy[i]) < .002f ;
                    // Both backends currently receive motor's [-1,1] projection.
                    valid &= std::abs(clip.z()/clip.w()-(plane == 0 ? -1.0f : 1.0f)) < .002f ;
                }
            }
        }
        return valid ;
    }

    class primitive_app : public motor::application::app
    {
        struct window_data
        {
            window_id_t id = window_id_t(-1) ;
            camera_t camera ;
            primitives_t debug ;
            motor::gfx::tri_render_3d_t solids ;
            motor::graphics::state_object_t clear, depth, overlay ;
            motor::math::vec2ui_t size = motor::math::vec2ui_t(960,720) ;
            std::atomic_size_t completed {0} ;
            bool_t dirty = true ;
        } ;

        options _opts ;
        bool_t * _passed ;
        window_data _windows[2] ;
        camera_t _perspective, _orthographic ;
        bool_t _paused = false, _orbit = true, _depth = true, _point = true,
            _sun = true, _cameras = true, _sphere = false, _empty = false ;
        bool_t _quitting = false, _valid = true, _reported = false ;
        float_t _time = 0, _yaw = .7f, _radius = 1.4f, _distance = 18, _far = 4.5f ;
        int_t _segments = 48 ;
        size_t _phase = 0 ;
        std::chrono::steady_clock::time_point _started ;

        size_t count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }

        void_t make_states( window_data & window, motor::string_cref_t suffix )
        {
            motor::graphics::render_state_sets_t state ;
            state.clear_s.do_change = true ;
            state.clear_s.ss.do_activate = true ;
            state.clear_s.ss.do_color_clear = true ;
            state.clear_s.ss.do_depth_clear = true ;
            state.clear_s.ss.clear_color = vec4_t(.035f,.045f,.055f,1) ;
            state.view_s.do_change = true ;
            state.view_s.ss.do_activate = true ;
            state.view_s.ss.vp = motor::math::vec4ui_t(0,0,960,720) ;
            window.clear = motor::graphics::state_object_t("primitive_clear_"+suffix) ;
            window.clear.add_render_state_set(state) ;
            for( size_t i = 0 ; i < 2 ; ++i )
            {
                motor::graphics::render_state_sets_t lines ;
                lines.depth_s.do_change = true ;
                lines.depth_s.ss.do_activate = i == 0 ;
                lines.depth_s.ss.do_depth_write = false ;
                auto & target = i == 0 ? window.depth : window.overlay ;
                target = motor::graphics::state_object_t("primitive_depth_"+suffix+motor::to_string(i)) ;
                target.add_render_state_set(lines) ;
            }
        }

        void_t fill( window_data & window )
        {
            auto & r = window.debug ;
            // Opaque geometry first, so line occlusion can be inspected independently.
            {
                auto & s = window.solids ;
                s.draw(vec3_t(-7,0,-6),vec3_t(7,0,6),vec3_t(7,0,-6),vec4_t(.14f,.18f,.20f,1)) ;
                s.draw(vec3_t(-7,0,-6),vec3_t(-7,0,6),vec3_t(7,0,6),vec4_t(.14f,.18f,.20f,1)) ;
                s.draw(vec3_t(-1,0,1),vec3_t(1,3,1),vec3_t(1,0,1),vec4_t(.28f,.32f,.35f,1)) ;
                s.draw(vec3_t(-1,0,1),vec3_t(-1,3,1),vec3_t(1,3,1),vec4_t(.28f,.32f,.35f,1)) ;
                s.draw_circle(motor::math::mat3f_t::make_identity(),vec3_t(0,1,-2),.55f,vec4_t(.85f,.3f,.15f,1),size_t(_segments)) ;
                r.draw_wire_circle(motor::math::mat3f_t::make_identity(),vec3_t(0,1,-2),.65f,vec4_t(1,.6f,.2f,1),size_t(_segments)) ;
            }
            {
                r.draw_lines(26,[]( size_t const i )
                {
                    float_t const p = float_t(i%13)-6 ;
                    return motor::gfx::line_render_3d_t::draw_line_data_t {
                        {i < 13 ? vec3_t(p,.015f,-6) : vec3_t(-6,.015f,p),
                         i < 13 ? vec3_t(p,.015f,6) : vec3_t(6,.015f,p)}, vec4_t(.3f,.36f,.38f,1)} ;
                }) ;
                r.draw_arrow(vec3_t(0,.04f,0),vec3_t(1,0,0),2,vec4_t(1,.2f,.2f,1)) ;
                r.draw_arrow(vec3_t(0,.04f,0),vec3_t(0,1,0),2,vec4_t(.2f,1,.3f,1)) ;
                r.draw_arrow(vec3_t(0,.04f,0),vec3_t(0,0,1),2,vec4_t(.3f,.5f,1,1)) ;
            }
            if( _point )
            {
                vec3_t const center(-3+std::sin(_time)*.6f,2,0) ;
                vec4_t const color(1,.78f,.18f,1) ;
                r.draw_wire_circle(center,window.camera.get_right(),window.camera.get_up(),_radius,color,size_t(_segments)) ;
                r.draw_line(center-vec3_t(.15f,0,0),center+vec3_t(.15f,0,0),color) ;
                r.draw_line(center-vec3_t(0,.15f,0),center+vec3_t(0,.15f,0),color) ;
                if( _sphere )
                {
                    r.draw_wire_sphere(center,_radius,color,size_t(_segments)) ;
                }
            }
            if( _sun )
            {
                vec3_t const direction(std::sin(_time*.4f)*.4f,-1,-.4f) ;
                for( int_t i = -1 ; i <= 1 ; ++i )
                    r.draw_arrow(vec3_t(3+float_t(i)*.5f,4,0),direction,2.3f,vec4_t(1,.9f,.65f,1)) ;
            }
            if( _cameras )
            {
                _valid &= validate_frustum(_perspective) ;
                _valid &= validate_frustum(_orthographic) ;
                r.draw_frustum(_perspective,vec4_t(.2f,.85f,1,1)) ;
                r.draw_frustum(_orthographic,vec4_t(1,.35f,.65f,1)) ;
            }
            if( _opts.smoke && _phase == 4 )
            {
                // Degenerate calls must not divide by zero or create invalid vertices.
                r.draw_wire_circle(vec3_t(0),vec3_t(1,0,0),vec3_t(0,1,0),1,vec4_t(1),0) ;
                r.draw_wire_circle(vec3_t(0),vec3_t(1,0,0),vec3_t(0,1,0),1,vec4_t(1),2) ;
                r.draw_wire_sphere(vec3_t(0),0,vec4_t(1)) ;
                r.draw_arrow(vec3_t(0),vec3_t(0),1,vec4_t(1)) ;
                r.draw_arrow(vec3_t(0),vec3_t(0,1,0),0,vec4_t(1)) ;
                // Short arrows clamp the head; a zero head draws just the shaft.
                r.draw_arrow(vec3_t(4,1,0),vec3_t(0,2,0),.2f,vec4_t(1)) ;
                r.draw_arrow(vec3_t(4,2,0),vec3_t(0,-2,0),.5f,vec4_t(1),0,0) ;
            }
        }

    public:
        primitive_app( options opts, bool_t * passed ) noexcept : _opts(opts), _passed(passed) {}
        primitive_app( primitive_app const & ) = delete ;
        primitive_app( primitive_app && rhs ) noexcept : primitive_app(rhs._opts,rhs._passed)
        {
            // motor::shared moves the app before initialization. Never move live
            // renderer instances: queued commands borrow their stable addresses.
            assert(rhs._windows[0].id == window_id_t(-1) && rhs._windows[1].id == window_id_t(-1)) ;
        }

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & window = _windows[i] ;
                auto const suffix = motor::to_string(i) ;
                window.debug.init("debug_primitives_"+suffix) ;
                window.solids.init("debug_solids_"+suffix) ;
                make_states(window,suffix) ;
                motor::application::window_info_t info ;
                bool_t const d3d = _opts.d3d || i == 1 ;
                info.window_name = d3d ? "motor | debug primitives | D3D11" : "motor | debug primitives | GL4" ;
                info.x = int_t(40+i*980) ; info.y = 60 ; info.w = 960 ; info.h = 720 ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                window.id = create_window(info) ;
                send_window_message(window.id,[]( auto & w )
                {
                    w.send_message(motor::application::show_message({true})) ;
                    w.send_message(motor::application::vsync_message_t({true})) ;
                }) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused && !_opts.still ) _time += data.sec_dt ;
            // Exercise orthographic -> perspective replacement during the smoke run.
            if( _opts.smoke && _phase == 3 ) _perspective.make_orthographic(2,1.5f,.4f,_far) ;
            _perspective.make_perspective_fov(1.6f,1,.75f,.4f,_far) ;
            _perspective.look_at(vec3_t(-3,2,4),vec3_t(0,1,0),vec3_t(-2,1,-1)) ;
            _orthographic.make_orthographic(2,1.5f,.4f,_far) ;
            _orthographic.look_at(vec3_t(3,2,4),vec3_t(0,1,0),vec3_t(2,1,-1)) ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & window = _windows[i] ;
                float_t const angle = _yaw + (_orbit ? _time*.12f : 0) ;
                window.camera.make_perspective_fov(float_t(window.size.x()),float_t(window.size.y()),.85f,.1f,1500) ;
                window.camera.look_at(vec3_t(std::sin(angle)*_distance,_distance*.55f,std::cos(angle)*_distance),vec3_t(0,1,0),vec3_t(0,1,0)) ;
                window.debug.set_view_proj(window.camera.mat_view(),window.camera.mat_proj()) ;
                window.solids.set_view_proj(window.camera.mat_view(),window.camera.mat_proj()) ;
                if( !_empty ) fill(window) ;
                window.solids.prepare_for_rendering() ;
                window.debug.prepare_for_rendering() ;
            }
            if( !_reported && !_empty )
            {
                motor::log::global_t::test("[primitives] camera corners project to expected clip-space corners",_valid) ;
                _reported = true ;
            }
            if( !_valid ) { _quitting = true ; close() ; }
        }

        void_t on_first_frame( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            w.solids.configure(fe) ; w.debug.configure(fe) ;
            fe->configure<motor::graphics::state_object_t>(&w.depth) ;
            fe->configure<motor::graphics::state_object_t>(&w.overlay) ;
        }

        void_t on_render( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            if( _quitting ) return ;
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            if( w.dirty )
            {
                w.clear.access_render_state(0,[&]( auto & s )
                { s.view_s.ss.vp = motor::math::vec4ui_t(0,0,w.size.x(),w.size.y()) ; return true ; }) ;
                fe->configure<motor::graphics::state_object_t>(&w.clear) ; w.dirty = false ;
            }
            w.solids.prepare_for_rendering(fe) ; w.debug.prepare_for_rendering(fe) ;
            fe->push(&w.clear) ;
            if( !_empty )
            {
                w.solids.render(fe) ;
                fe->push(_depth ? &w.depth : &w.overlay) ;
                w.debug.render(fe) ;
                fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            }
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            fe->fence([&w]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase == motor::graphics::gen4::frontend::fence_phase::execute ) w.completed.fetch_add(1) ;
            }) ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < count() ; ++i )
                if( _windows[i].completed.load() < 24 ) return ;
            motor::log::global_t::test("[primitives] submitted phase "+motor::to_string(_phase)+" completed",_valid) ;
            if( ++_phase == 5 ) { *_passed = _valid ; _quitting = true ; close() ; return ; }
            for( size_t i = 0 ; i < count() ; ++i ) _windows[i].completed.store(0) ;
            _segments = _phase == 1 ? 128 : 12 ;
            _sphere = _phase == 1 ; _depth = _phase != 1 ;
            _empty = _phase == 2 ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            if( ImGui::Begin("Debug primitives",nullptr,ImGuiWindowFlags_AlwaysAutoResize) )
            {
                ImGui::BeginDisabled(_opts.smoke) ;
                ImGui::Checkbox("Point marker",&_point) ;
                ImGui::Checkbox("Range sphere",&_sphere) ;
                ImGui::Checkbox("Directional arrows",&_sun) ;
                ImGui::Checkbox("Camera volumes",&_cameras) ;
                ImGui::Checkbox("Depth test",&_depth) ;
                ImGui::Checkbox("Empty frame",&_empty) ;
                ImGui::Checkbox("Pause",&_paused) ; ImGui::Checkbox("Orbit",&_orbit) ;
                ImGui::SliderFloat("Radius",&_radius,.2f,3) ;
                ImGui::SliderInt("Segments",&_segments,8,128) ;
                ImGui::SliderFloat("Camera far",&_far,1,8) ;
                ImGui::SliderFloat("View yaw",&_yaw,-3.14f,3.14f) ;
                ImGui::SliderFloat("View distance",&_distance,8,30) ;
                ImGui::EndDisabled() ;
            }
            ImGui::End() ; return true ;
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
                w.size = motor::math::vec2ui_t(uint_t(event.resize_msg.w),uint_t(event.resize_msg.h)) ; w.dirty = true ;
            }
            if( event.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting && std::chrono::steady_clock::now()-_started > std::chrono::seconds(45) )
            {
                motor::log::global_t::test("[primitives] smoke timeout",false) ;
                _quitting = true ; close() ;
            }
        }

        void_t on_last_frame( window_id_t const id, frontend_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            fe->release<motor::graphics::state_object_t>(&w.clear) ;
            fe->release<motor::graphics::state_object_t>(&w.depth) ;
            fe->release<motor::graphics::state_object_t>(&w.overlay) ;
            // Primitive renderers have no frontend release API. Their objects stay
            // alive until backend shutdown; window teardown releases GPU resources.
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            for( size_t i = 0 ; i < count() ; ++i )
            { _windows[i].debug.release() ; _windows[i].solids.release() ; }
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else if( arg == "--dual" ) { opts.dual = true ; opts.d3d = false ; }
        else
        {
            motor::log::global_t::status("Usage: 17_debug_primitives [--gl-only|--d3d-only|--dual] [--smoke] [--still]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.dual || opts.d3d )
    {
        motor::log::global_t::error("D3D11 requires Windows.") ;
        motor::log::global_t::deinit() ; return 2 ;
    }
#endif
    bool passed = false ;
    auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::primitive_app(opts,&passed))) ;
    int const code = carrier->exec() ;
    motor::release(motor::move(carrier)) ;
    motor::log::global_t::test("[primitives] lifecycle completed; pixels require visual verification",code == 0 && (!opts.smoke || passed)) ;
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const leaked = motor::memory::global_t::dump_to_std() ;
    return code != 0 ? code : leaked != 0 || (opts.smoke && !passed) ? 1 : 0 ;
}
