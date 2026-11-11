#include <motor/platform/global.h>
#include <motor/audio/object/capture_object.hpp>
#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/tool/structs.h>
#include <motor/concurrent/global.h>
#include <motor/memory/global.h>
#include <motor/log/global.h>
#include <mutex>
#include <memory>
#include "capture_options.hpp"
#include "onset_signals.hpp"
#include "onset_meter.hpp"

namespace onset_sample
{
    using vec3_t = motor::math::vec3f_t;
    using vec4_t = motor::math::vec4f_t;
    struct options
    {
        sample_support::capture_options capture;
        bool_t d3d = false, demo = false, synthetic = false, cpu = false;
    };

    class app : public motor::application::app
    {
        options _options;
        motor::audio::capture_object_t _capture;
        motor::vector< float_t > _spectrum;
        std::array< band, 3 > _bands;
        std::array< imgui_onset_meter, 3 > _meters;
        std::unique_ptr< std::mutex > _audio_mutex = std::make_unique< std::mutex >();
        bool_t _synthetic = false, _audio_configured = false, _has_spectrum = false;
        double_t _synthetic_time = 0.0, _accumulator = 0.0;
        float_t _bin_hz = 0.0f, _time = 0.0f;
        motor::gfx::primitive_render_3d_t _primitives;
        motor::gfx::generic_camera_t _camera;
        motor::graphics::state_object_t _state { "onset30.clear" };
        uint_t _width = 1280, _height = 720;
        bool_t _dirty = true;

        void_t reset( void_t ) noexcept
        {
            for( auto & b : _bands ) b.reset();
            _synthetic_time = _accumulator = 0.0;
            _spectrum.clear();
            _has_spectrum = false;
        }

        void_t cube( vec3_t const center, float_t const half, float_t const angle, vec4_t const color )
        {
            std::array< vec3_t, 8 > points;
            for( size_t i = 0; i < points.size(); ++i )
            {
                float_t const x = (i & 1) ? half : -half;
                float_t const y = (i & 2) ? half : -half;
                float_t const z = (i & 4) ? half : -half;
                points[i] = center + vec3_t( x*std::cos(angle)+z*std::sin(angle), y,
                    -x*std::sin(angle)+z*std::cos(angle) );
            }
            size_t const faces[6][4] = { {0,1,3,2}, {4,6,7,5}, {0,4,5,1},
                {2,3,7,6}, {0,2,6,4}, {1,5,7,3} };
            float_t const shades[] = { .5f, .75f, .45f, 1.0f, .65f, .85f };
            for( size_t f = 0; f < 6; ++f )
            {
                auto const & face = faces[f];
                auto const c = vec4_t(color.xyz()*shades[f], 1.0f);
                _primitives.draw_tri(points[face[0]],points[face[1]],points[face[2]],c);
                _primitives.draw_tri(points[face[0]],points[face[2]],points[face[3]],c);
            }
        }

        void_t ring( vec3_t const center, float_t const radius, float_t const thickness, vec4_t const color )
        {
            for( size_t i = 0; i < 64; ++i )
            {
                float_t const a = float_t(i)*6.2831853f/64.0f;
                float_t const b = float_t(i+1)*6.2831853f/64.0f;
                auto const p = center + vec3_t(std::cos(a)*radius,0,std::sin(a)*radius);
                auto const q = center + vec3_t(std::cos(b)*radius,0,std::sin(b)*radius);
                auto const r = center + vec3_t(std::cos(a)*(radius+thickness),0,std::sin(a)*(radius+thickness));
                auto const s = center + vec3_t(std::cos(b)*(radius+thickness),0,std::sin(b)*(radius+thickness));
                _primitives.draw_tri(p,q,r,color);
                _primitives.draw_tri(q,s,r,color);
            }
        }

      public:
        explicit app( options const & opts ) : _options(opts), _synthetic(opts.synthetic) {}

        void_t on_init( void_t ) noexcept override
        {
            motor::application::window_info_t wi;
            wi.x = 40; wi.y = 40; wi.w = 1280; wi.h = 720;
            wi.window_name = "motor | Audio onset | low / mid / high";
            wi.gen = _options.d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4;
            _options.capture.apply(wi);
            _width = uint_t(wi.w); _height = uint_t(wi.h);
            send_window_message(create_window(wi), [](motor::application::app::window_view & window)
            {
                window.send_message(motor::application::show_message({true}));
                window.send_message(motor::application::vsync_message_t({true}));
            });
            _primitives.init("onset30.primitives");
            motor::graphics::render_state_sets_t state;
            state.clear_s.do_change = true;
            state.clear_s.ss.do_activate = true;
            state.clear_s.ss.do_color_clear = state.clear_s.ss.do_depth_clear = true;
            state.clear_s.ss.clear_color = vec4_t(.018f,.022f,.028f,1);
            state.view_s.do_change = true;
            state.view_s.ss.do_activate = true;
            state.view_s.ss.vp = motor::math::vec4ui_t(0,0,_width,_height);
            _state.add_render_state_set(state);
            motor::log::global_t::status(_synthetic ? "[onset] Synthetic band pulses; no audio is played or captured." :
                "[onset] System output capture; play music on the default output device.");
        }

        void_t on_audio( motor::audio::frontend_ptr_t fe, audio_data_in_t data ) noexcept override
        {
            std::lock_guard< std::mutex > lock(*_audio_mutex);
            if( _synthetic )
            {
                if( _audio_configured ) { fe->capture(&_capture,false); _audio_configured = false; }
                return;
            }
            if( !_audio_configured )
            {
                fe->configure(motor::audio::capture_type::what_u_hear,&_capture);
                _audio_configured = true;
            }
            fe->capture(&_capture);
            // As in motor_suites: consume the previous completed capture, then queue the next.
            _capture.copy_frequencies_to(_spectrum);
            _bin_hz = _capture.get_bin_width_hz< float_t >();
            _has_spectrum = !_spectrum.empty() && std::isfinite(_bin_hz) && _bin_hz > 0;
            auto const levels = sum_bands(_spectrum,_bin_hz);
            for( size_t b = 0; b < 3; ++b ) _bands[b].update(levels[b],data.sec_dt);
        }

        void_t on_graphics( graphics_data_in_t data ) noexcept override
        {
            _time += data.sec_dt;
            std::array< float_t, 3 > pulses {}, levels {};
            {
                std::lock_guard< std::mutex > lock(*_audio_mutex);
                if( _synthetic )
                {
                    // Detector cadence is independent of the rendering frame interval.
                    _accumulator += std::min(double_t(data.sec_dt),0.25);
                    while( _accumulator >= 0.005 )
                    {
                        auto const values = synthetic_levels(_synthetic_time);
                        for( size_t b = 0; b < 3; ++b ) _bands[b].update(values[b],0.005f);
                        _synthetic_time += 0.005;
                        _accumulator -= 0.005;
                    }
                }
                for( size_t b = 0; b < 3; ++b )
                {
                    pulses[b] = _bands[b].pulse;
                    levels[b] = std::clamp(_bands[b].detector->envelope_value()/0.012f,0.0f,1.0f);
                }
            }
            float_t const aspect = float_t(_width)/float_t(_height);
            // Preserve horizontal coverage in portrait capture.
            float_t const fov = 2.0f*std::atan(std::tan(.42f)/std::min(aspect,1.0f));
            _camera.make_perspective_fov(aspect,1,fov,.1f,100);
            _camera.look_at(vec3_t(1.2f*std::sin(_time*.08f),7.5f,14),vec3_t(0,1,0),vec3_t(0,1,0));
            _primitives.set_view_proj(_camera.mat_view(),_camera.mat_proj());
            vec4_t const colors[] = { vec4_t(1,.32f,.16f,1),vec4_t(.15f,.8f,.65f,1),vec4_t(.5f,.55f,1,1) };
            for( size_t i = 0; i < 3; ++i )
            {
                auto const base = vec3_t((float_t(i)-1)*3.8f,0,0);
                auto const color = colors[i];
                cube(base+vec3_t(0,-.3f,0),1.05f,0,vec4_t(.07f,.09f,.11f,1));
                cube(base+vec3_t(0,1.4f+pulses[i]*1.2f,0),.5f+.28f*pulses[i],_time*.3f+float_t(i),color);
                for( size_t j = 0; j < 3; ++j )
                {
                    float_t const brightness = .22f+.78f*pulses[i];
                    ring(base+vec3_t(0,.8f+float_t(j)*.17f,0),1.0f+float_t(j)*.16f+.25f*pulses[i],.04f,
                        vec4_t(color.xyz()*brightness,1));
                }
                // A separate energy pillar distinguishes sustained level from onset events.
                for( size_t j = 0; j < 12; ++j )
                {
                    float_t const brightness = float_t(j)/12.0f < levels[i] ? .85f : .12f;
                    cube(base+vec3_t(1.1f,1.0f+float_t(j)*.15f,-.5f),.05f,0,vec4_t(color.xyz()*brightness,1));
                }
            }
            for( int_t i = -8; i <= 8; ++i )
            {
                _primitives.draw_line(vec3_t(float_t(i),-.02f,-4),vec3_t(float_t(i),-.02f,4),vec4_t(.09f,.12f,.14f,1));
            }
            _primitives.prepare_for_rendering();
        }

        void_t on_event( window_id_t const, motor::application::window_message_listener::state_vector_cref_t sv ) noexcept override
        {
            if( sv.close_changed ) close();
            if( sv.resize_changed && sv.resize_msg.resize && sv.resize_msg.w > 0 && sv.resize_msg.h > 0 )
            { _width = uint_t(sv.resize_msg.w); _height = uint_t(sv.resize_msg.h); _dirty = true; }
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe, render_data_in_t ) noexcept override
        {
            _primitives.configure(fe);
        }

        void_t on_render( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe, render_data_in_t ) noexcept override
        {
            if( _dirty )
            {
                _state.access_render_state(0,[this](auto & state)
                { state.view_s.ss.vp = motor::math::vec4ui_t(0,0,_width,_height); return true; });
                fe->configure<motor::graphics::state_object_t>(&_state);
                _dirty = false;
            }
            _primitives.prepare_for_rendering(fe);
            fe->push(&_state);
            _primitives.render(fe);
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state);
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe, render_data_in_t ) noexcept override
        {
            _primitives.release(fe);
            fe->release<motor::graphics::state_object_t>(&_state);
        }

        bool_t on_tool( window_id_t const, tool_data_ref_t ) noexcept override
        {
            if( _options.demo ) return false;
            std::lock_guard< std::mutex > lock(*_audio_mutex);
            if( ImGui::Begin("Onset") )
            {
                int_t source = _synthetic ? 1 : 0;
                if( ImGui::Combo("Source",&source,"System audio\0Synthetic pulses\0") )
                { _synthetic = source == 1; reset(); }
                if( ImGui::Button("Reset detectors") ) reset();
                if( !_synthetic ) ImGui::Text("Capture: %s | %.2f Hz/bin",_has_spectrum ? "spectrum available" : "waiting",double_t(_bin_hz));
                char const * labels[] = { "40\n250\nHz", "250\n2000\nHz", "2000\n12000\nHz" };
                for( size_t i = 0; i < 3; ++i )
                {
                    if( i ) ImGui::SameLine();
                    ImGui::BeginGroup();
                    _meters[i].draw(labels[i],_bands[i].raw,*_bands[i].detector,_bands[i].pulse);
                    ImGui::Text("%zu",_bands[i].count);
                    ImGui::EndGroup();
                }
                float_t decay = _bands[0].decay;
                if( ImGui::SliderFloat("Pulse release",&decay,.05f,1.0f,"%.2f s") )
                    for( auto & band : _bands ) band.decay = decay;
            }
            ImGui::End();
            return true;
        }

        void_t on_shutdown( void_t ) noexcept override { _primitives.release(); }
    };
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types;
    onset_sample::options opts;
    bool_t help = false;
    for( int i = 1; i < argc; ++i )
    {
        motor::string_t const arg(argv[i]);
        if( arg == "--gl-only" ) opts.d3d = false;
        else if( arg == "--d3d-only" ) opts.d3d = true;
        else if( arg == "--demo" ) opts.demo = true;
        else if( arg == "--synthetic" ) opts.synthetic = true;
        else if( arg == "--cpu-only" ) opts.cpu = true;
        else if( arg == "--help" ) help = true;
        else if( arg == "--youtube-short" && i+1 < argc && opts.capture.parse(argv[i+1]) ) ++i;
        else
        {
            motor::log::global_t::error("Unknown or incomplete option: "+arg);
            motor::log::global_t::deinit(); return 2;
        }
    }
    if( help )
    {
        motor::log::global_t::status("30_audio_onset [--gl-only|--d3d-only] [--synthetic] [--demo] [--youtube-short square|vertical] [--cpu-only]");
        motor::log::global_t::deinit(); return 0;
    }
    if( opts.cpu )
    {
        bool_t const ok = onset_sample::checks();
        motor::log::global_t::deinit(); return ok ? 0 : 1;
    }
#ifndef _WIN32
    if( opts.d3d )
    {
        motor::log::global_t::error("d3d11 requires Windows.");
        motor::log::global_t::deinit(); return 2;
    }
#endif
    motor::application::carrier_mtr_t carrier =
        motor::platform::global_t::create_carrier(motor::shared(onset_sample::app(opts)));
    auto const result = carrier->exec();
    motor::memory::release_ptr(carrier);
    motor::concurrent::global_t::deinit();
    motor::log::global_t::deinit();
    motor::memory::global_t::dump_to_std();
    return result;
}
