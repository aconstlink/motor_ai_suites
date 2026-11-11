#include "../suite_wire/ring_filters.hpp"
#include "capture_options.hpp"
#include <motor/platform/global.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/gfx/primitive/tri_render_3d.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <algorithm>
#include <memory>

namespace ring_sample
{
    using namespace motor::core::types ;
    using v3 = motor::math::vec3f_t ;
    using v4 = motor::math::vec4f_t ;
    using fe_t = motor::graphics::gen4::frontend_ptr_t ;
    struct options { sample_support::capture_options capture ; bool d3d=false, automatic=false, demo=false ; } ;

    v3 station(int i,bool portrait)
    {
        return portrait ? v3(0,3.15f*(1-i),0) : v3(3.25f*(i-1),0,0) ;
    }

    void frame_stations(motor::gfx::generic_camera_t & camera,float width,float height)
    {
        float const aspect=width/height ;
        bool const portrait=aspect<.9f ;
        float const dist=portrait ? std::max(14.f,4.8f/aspect) : std::max(7.f,12.6f/aspect) ;
        camera.make_perspective_fov(width,height,.78f,.1f,100.f) ;
        camera.look_at(v3(0,.24f*dist,dist),v3(0,1,0),v3(0)) ;
    }

    bool check_camera(float width,float height)
    {
        motor::gfx::generic_camera_t camera ;
        frame_stations(camera,width,height) ;
        auto vp=camera.mat_proj()*camera.mat_view() ;
        auto inside=[](v4 const & p){ return p.w()>0 && std::abs(p.x())<p.w()
            && std::abs(p.y())<p.w() && std::abs(p.z())<p.w(); } ;
        for(int i=0;i<3;++i) for(int k=0;k<8;++k)
        {
            auto const p=station(i,width/height<.9f)+v3(k&1 ? 1.25f : -1.25f,
                k&2 ? 1.65f : -1.15f,k&4 ? 1.f : -1.f) ;
            if(!inside(vp*v4(p,1))) return false ;
        }
        auto c=vp*v4(0,0,0,1), x=vp*v4(1,0,0,1), y=vp*v4(0,1,0,1) ;
        return inside(c) && std::abs(c.x()/c.w())<.001f && std::abs(c.y()/c.w())<.001f
            && std::abs(x.x()/x.w())>.05f && std::abs(y.y()/y.w())>.05f ;
    }

    class rings_app : public motor::application::app
    {
        options _opts ;
        std::unique_ptr<filters> _signals ;
        motor::gfx::tri_render_3d_t _tris ;
        motor::gfx::generic_camera_t _camera ;
        motor::graphics::state_object_t _clear {"rings.clear"} ;
        size_t _width=960, _height=720 ;
        float _time=0, _next=0, _strength=.75f ;
        bool _kick=false, _paused=false, _dirty=true ;

        void block(v3 center,v3 half,float yaw,float lean,v3 color)
        {
            v3 points[8] ;
            float const cy=std::cos(yaw), sy=std::sin(yaw), cr=std::cos(lean), sr=std::sin(lean) ;
            for(int i=0;i<8;++i)
            {
                float x=(i&1)?half.x():-half.x(), y=(i&2)?half.y():-half.y(), z=(i&4)?half.z():-half.z() ;
                float h=cy*x+sy*z ;
                points[i]=center+v3(cr*h-sr*y,sr*h+cr*y,cy*z-sy*x) ;
            }
            int const faces[][4]={{0,2,3,1},{4,5,7,6},{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3}} ;
            float const shade[]={.52f,.85f,.61f,.71f,.40f,1.f} ;
            for(int f=0;f<6;++f)
            {
                auto const & v=faces[f] ;
                auto c=v4(color*shade[f],1) ;
                _tris.draw(points[v[0]],points[v[1]],points[v[2]],c) ;
                _tris.draw(points[v[0]],points[v[2]],points[v[3]],c) ;
            }
        }
    public:
        explicit rings_app(options opt) : _opts(opt) {}
        rings_app(rings_app && rhs) noexcept : rings_app(rhs._opts) {}
        void_t on_init(void_t) noexcept override
        {
            _signals=std::make_unique<filters>() ;
            if(!_signals->valid()) { motor::log::global_t::error("Ring slot connection failed"); close(); return; }
            _tris.init("trigger.sculptures") ;
            motor::application::window_info_t w ;
            w.w=960; w.h=720; _opts.capture.apply(w); _width=w.w; _height=w.h;
            w.window_name="motor | Trigger sculptures" ;
            using gen=motor::application::graphics_generation ;
            w.gen=_opts.d3d ? gen::gen4_d3d11 : gen::gen4_gl4 ;
            motor::graphics::render_state_sets_t state ;
            state.view_s.do_change=state.view_s.ss.do_activate=true ;
            state.clear_s.do_change=state.clear_s.ss.do_activate=true ;
            state.clear_s.ss.do_color_clear=state.clear_s.ss.do_depth_clear=true ;
            state.clear_s.ss.clear_color=v4(.025f,.030f,.045f,1) ;
            _clear.add_render_state_set(state) ;
            send_window_message(create_window(w),[](auto & wnd){
                wnd.send_message(motor::application::show_message({true})) ;
                wnd.send_message(motor::application::vsync_message_t({true})) ;
            }) ;
        }
        void_t on_event(window_id_t, motor::application::window_message_listener::state_vector_cref_t e) noexcept override
        {
            if(e.resize_changed && e.resize_msg.resize && e.resize_msg.w>0 && e.resize_msg.h>0)
            { _width=e.resize_msg.w; _height=e.resize_msg.h; _dirty=true; }
            if(e.close_changed) close() ;
        }
        void_t on_graphics(motor::application::app::graphics_data_in_t gd) noexcept override
        {
            if(!_signals) return ;
            if(!_paused) _time+=gd.sec_dt ;
            bool beat=_opts.automatic && !_paused && _time>=_next ;
            if(beat) _next=_time+2.5f ;
            _signals->step(_time,_strength,_kick||beat) ; _kick=false ;
            frame_stations(_camera,float(_width),float(_height)) ;
            _tris.set_view_proj(_camera.mat_view(),_camera.mat_proj()) ;
            float const level[]={_signals->tilt()/20.f,_signals->glow(),_signals->latched_tilt()/40.f} ;
            v3 const colors[]={v3(1,.42f,.17f),v3(.18f,.89f,.72f),v3(.64f,.56f,1)} ;
            for(int i=0;i<3;++i)
            {
                auto p=station(i,float(_width)/float(_height)<.9f) ;
                auto color=colors[i] ;
                block(p+v3(0,-.96f,0),v3(1.18f,.14f,.83f),0,0,v3(.16f,.19f,.23f)) ;
                block(p+v3(0,-.81f,0),v3(1.1f,.025f,.78f),0,0,color*(.28f+.55f*level[i])) ;
                float lift=i==0 ? .95f*level[i] : 0.f ;
                float lean=i==2 ? _signals->latched_tilt()*.01745329252f : .08f ;
                float brightness=i==1 ? .22f+.78f*level[i] : .9f ;
                block(p+v3(-.18f,.04f+lift,0),v3(.44f,.54f,.44f),.55f,lean,color*brightness) ;
                for(int j=0;j<10;++j)
                    block(p+v3(.92f,-.72f+.145f*j,0),v3(.1f,.054f,.1f),0,0,
                        level[i]>(float(j)+.5f)/10.f ? color : v3(.14f,.17f,.2f)) ;
            }
            _tris.prepare_for_rendering() ;
        }
        void_t on_first_frame(window_id_t,fe_t fe,motor::application::app::render_data_in_t) noexcept override
        { _tris.configure(fe); }
        void_t on_render(window_id_t,fe_t fe,motor::application::app::render_data_in_t) noexcept override
        {
            if(_dirty)
            {
                _clear.access_render_state(0,[&](auto & s){ s.view_s.ss.vp=motor::math::vec4ui_t(0,0,uint_t(_width),uint_t(_height)); return true; }) ;
                fe->configure<motor::graphics::state_object_t>(&_clear); _dirty=false;
            }
            _tris.prepare_for_rendering(fe); fe->push(&_clear); _tris.render(fe);
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state);
        }
        bool_t on_tool(window_id_t,motor::application::app::tool_data_ref_t) noexcept override
        {
            if(_opts.demo) return false ;
            if(ImGui::Begin("Trigger signals",nullptr,ImGuiWindowFlags_AlwaysAutoResize))
            {
                if(ImGui::Button("Kick")) _kick=true ;
                ImGui::SameLine(); ImGui::Checkbox("Auto",&_opts.automatic);
                ImGui::SameLine(); ImGui::Checkbox("Pause clock",&_paused);
                ImGui::SliderFloat("Kick strength",&_strength,0,1);
            }
            ImGui::End(); return true;
        }
        void_t on_last_frame(window_id_t,fe_t fe,motor::application::app::render_data_in_t) noexcept override
        { fe->release<motor::graphics::state_object_t>(&_clear); }
        void_t on_shutdown(void_t) noexcept override { _tris.release(); _signals.reset(); }
    } ;
}

int main(int argc,char ** argv)
{
    ring_sample::options opts ;
    for(int i=1;i<argc;++i)
    {
        motor::string_t arg(argv[i]) ;
        if(arg=="--check-camera")
        {
            bool ok=ring_sample::check_camera(960,720) && ring_sample::check_camera(1080,1080)
                && ring_sample::check_camera(1080,1920) ;
            motor::log::global_t::test("Sculpture framing: landscape, square, vertical",ok) ;
            motor::log::global_t::deinit(); return ok ? 0 : 1 ;
        }
        else if(arg=="--demo") opts.demo=opts.automatic=true ;
        else if(arg=="--d3d-only") opts.d3d=true ;
        else if(arg=="--gl-only") opts.d3d=false ;
        else if(arg=="--youtube-short" && i+1<argc && opts.capture.parse(argv[++i])) {}
        else
        {
            motor::log::global_t::status("31_trigger_rings [--demo] [--gl-only|--d3d-only] [--youtube-short square|vertical]") ;
            motor::log::global_t::deinit(); return arg=="--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if(opts.d3d) return 2 ;
#endif
    auto carrier=motor::platform::global_t::create_carrier(motor::shared(ring_sample::rings_app(opts))) ;
    int code=carrier->exec() ;
    motor::release(motor::move(carrier)); motor::concurrent::global_t::deinit();
    motor::profiling::global_t::deinit(); motor::io::global_t::deinit(); motor::log::global_t::deinit();
    return code ? code : motor::memory::global_t::dump_to_std()!=0 ;
}
