#include "capture_options.hpp"

#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/io/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/std/vector>

#include <chrono>

namespace sample
{
    using namespace motor::core::types ;
    namespace g = motor::graphics ;
    using states_t = g::render_state_sets_t ;
    using fe_t = g::gen4::frontend_ptr_t ;
    using vec4_t = motor::math::vec4f_t ;

    bool_t cpu_checks( void_t )
    {
        bool_t passed = true ;
        auto check = [&]( char const * name, bool_t value )
        {
            motor::log::global_t::test( motor::string_t("[states CPU] ") + name, value ) ;
            passed = value && passed ;
        } ;
        states_t parent, child ;
        parent.depth_s.do_change = true ;
        parent.depth_s.ss.do_activate = true ;
        parent.depth_s.ss.do_depth_write = true ;
        parent.clear_s.do_change = true ;
        parent.clear_s.ss.do_activate = true ;
        child.blend_s.do_change = true ;
        child.blend_s.ss.do_activate = true ;
        auto const merged = parent + child ;
        check( "partial push preserves depth", merged.depth_s.ss.do_activate && merged.depth_s.ss.do_depth_write ) ;
        check( "partial push adds blending", merged.blend_s.do_change && merged.blend_s.ss.do_activate ) ;
        check( "partial push does not repeat clear", !merged.clear_s.do_change ) ;
        auto const restored = merged - parent ;
        check( "pop disables child blending", restored.blend_s.do_change && !restored.blend_s.ss.do_activate ) ;
        check( "pop never clears", !restored.clear_s.do_change ) ;
        child.depth_s.do_change = true ;
        child.depth_s.ss.do_activate = false ;
        auto const disabled = parent + child ;
        check( "explicit disable overrides parent", !disabled.depth_s.ss.do_activate ) ;
        check( "pop restores depth", (disabled - parent).depth_s.ss.do_activate ) ;
        g::state_object_t object("states.cpu") ;
        object.add_render_state_set(parent).add_render_state_set(child) ;
        size_t count = 0 ;
        object.access_render_state(1, []( states_t & s ) { s.depth_s.ss.do_activate = true ; return true ; }) ;
        object.for_each([&](size_t i, states_t const & s)
        {
            if( i == 1 ) check("indexed state mutation", s.depth_s.ss.do_activate) ;
            ++count ;
        }) ;
        check("multiple state sets retained", count == 2 && object.size() == 2) ;
        return passed ;
    }

    struct options
    {
        sample_support::capture_options capture ;
        bool_t dual = false, d3d = false, smoke = false, cpu = false ;
    } ;

    class render_states_app : public motor::application::app
    {
        enum : size_t { root, less, equal, no_write, alpha, additive, back, front, wire, scissor, viewport, state_count } ;
        enum class phase { configure, active, release } ;
        struct vertex { motor::math::vec3f_t position ; } ;
        struct draw { size_t vs = 0, geo = 0 ; } ;
        struct window
        {
            window_id_t id = window_id_t(-1) ;
            g::state_object_t states ;
            uint_t width = 800, height = 720 ;
            bool_t dirty = true ;
            phase state = phase::configure ;
            size_t revision = 0, frames = 0, cycles = 0 ;
        } ;
        options _opts ;
        bool_t * _passed ;
        bool_t _failed = false, _closing = false, _alternate = false ;
        size_t _revision = 0, _step = 0 ;
        bool_t _release_request = false ;
        window _windows[2] ;
        g::geometry_object_t _quad, _reverse ;
        g::msl_object_mtr_t _shader = nullptr ;
        draw _background[12], _first[12], _second[12], _marker[12], _viewport_draw ;
        std::chrono::steady_clock::time_point _started ;
        char const * _labels[12] = { "Depth off", "Depth LESS", "Depth LESS_EQUAL", "Depth write off",
            "Opaque", "Alpha", "Additive", "Viewport + pop", "Cull back", "Cull front", "Wireframe", "Scissor + pop" } ;
        struct description
        {
            char const * order ;
            char const * depth ;
            char const * state ;
            char const * expected ;
        } ;
        description const _descriptions[12] =
        {
            { "1 Orange, 2 Blue.", "Orange is nearer; blue is farther.",
                "Depth test off.", "Blue covers orange in the overlap, despite being farther away." },
            { "1 Orange, 2 Blue.", "Orange is nearer; blue is farther.",
                "LESS, depth writes on for both draws.", "Orange remains visible in the overlap. Blue is rejected there." },
            { "1 Orange, 2 Blue.", "Both have exactly the same depth.",
                "LESS_EQUAL, depth writes on.", "Blue covers orange in the overlap because equal depth passes." },
            { "1 Orange with depth writes OFF, 2 Blue with depth writes ON.", "Orange is nearer; blue is farther.",
                "LESS for both draws; the inner state is popped before blue.", "Blue covers orange: orange did not store its depth." },
            { "1 Blue, 2 Orange.", "Blue is nearer; orange is farther. Depth test off.",
                "Blending off; orange still has alpha 0.5.", "Orange completely replaces blue in the overlap; alpha is ignored for blending." },
            { "1 Blue, 2 Orange.", "Blue is nearer; orange is farther. Depth test off.",
                "Orange uses SRC_ALPHA / ONE_MINUS_SRC_ALPHA, alpha 0.5.", "Orange mixes equally with blue in the overlap and with the background elsewhere." },
            { "1 Blue, 2 Orange.", "Blue is nearer; orange is farther. Depth test off.",
                "Orange uses additive blending: ONE / ONE.", "Colors add; the overlap is brighter. Orange's alpha does not scale its RGB." },
            { "1 Blue inside the small viewport, 2 Pop, 3 Green marker.", "Depth test off.",
                "The viewport is restored before the green marker.", "Blue stays inside the small viewport; green appears at its normal position outside it." },
            { "1 Blue, 2 Orange; opposite triangle winding.", "Both have the same depth. Depth test off.",
                "Back faces culled; blue is front-facing, orange back-facing.", "Only blue is visible. The green marker remains visible after pop." },
            { "1 Blue, 2 Orange; opposite triangle winding.", "Both have the same depth. Depth test off.",
                "Front faces culled; blue is front-facing, orange back-facing.", "Only orange is visible. The green marker remains visible after pop." },
            { "1 Blue, 2 Orange, 3 Pop, 4 Green marker.", "Blue is nearer; orange is farther. Depth test off.",
                "Wireframe enabled for both rectangles.", "Only edges and triangle diagonals are drawn; the green marker is solid." },
            { "1 Blue, 2 Orange inside the scissor rectangle, 3 Pop, 4 Green marker.", "Blue is nearer; orange is farther. Depth test off.",
                "Scissor clips both rectangles, then is disabled by pop.", "Only a narrow region remains; orange covers blue in the overlap. Green is not clipped." }
        } ;

        size_t count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        static float_t cx( size_t i ) noexcept { return -0.72f + float_t(i % 4) * 0.48f ; }
        static float_t cy( size_t i ) noexcept { return 0.43f - float_t(i / 4) * 0.53f ; }
        void_t fail( char const * message ) noexcept
        {
            motor::log::global_t::test(message, false) ;
            _failed = true ; _closing = true ; close() ;
        }
        static g::geometry_object_t geometry( bool_t reverse )
        {
            auto vb = g::vertex_buffer_t().add_layout_element(g::vertex_attribute::position,
                g::type::tfloat, g::type_struct::vec3).resize(4).update<vertex>([](vertex * v, size_t)
            {
                v[0].position = motor::math::vec3f_t(-1.0f, -1.0f, 0.0f) ;
                v[1].position = motor::math::vec3f_t(1.0f, -1.0f, 0.0f) ;
                v[2].position = motor::math::vec3f_t(1.0f, 1.0f, 0.0f) ;
                v[3].position = motor::math::vec3f_t(-1.0f, 1.0f, 0.0f) ;
            }) ;
            auto ib = g::index_buffer_t().set_layout_element(g::type::tuint).resize(6)
                .update<uint_t>([reverse](uint_t * v, size_t)
            {
                uint_t const indices[] = {0, 1, 2, 0, 2, 3} ;
                for( size_t i = 0 ; i < 6 ; i += 3 )
                {
                    v[i] = indices[i] ; v[i+1] = indices[i+(reverse ? 2 : 1)] ;
                    v[i+2] = indices[i+(reverse ? 1 : 2)] ;
                }
            }) ;
            return g::geometry_object_t(reverse ? "states.reverse" : "states.quad",
                g::primitive_type::triangles, std::move(vb), std::move(ib)) ;
        }
        draw make_draw( vec4_t const & rect, float_t z, vec4_t const & color, size_t geo = 0 )
        {
            auto vs = motor::shared(g::variable_set_t()) ;
            vs->data_variable<vec4_t>("u_rect")->set(rect) ;
            vs->data_variable<vec4_t>("u_color")->set(color) ;
            vs->data_variable<float_t>("u_depth")->set(z) ;
            return {_shader->add_variable_set(motor::move(vs)), geo} ;
        }
        static motor::math::vec4ui_t pixel_rect( window const & w, float_t x, float_t y, float_t hx, float_t hy )
        {
            return motor::math::vec4ui_t(uint_t((x-hx+1.0f)*0.5f*float_t(w.width)),
                uint_t((y-hy+1.0f)*0.5f*float_t(w.height)),
                uint_t(hx*float_t(w.width)), uint_t(hy*float_t(w.height))) ;
        }
        void_t make_states( window & w )
        {
            states_t s[state_count] ;
            auto & base = s[root] ;
            base.clear_s.do_change = true ; base.clear_s.ss.do_activate = true ;
            base.clear_s.ss.do_color_clear = true ; base.clear_s.ss.do_depth_clear = true ;
            base.clear_s.ss.clear_color = vec4_t(0.025f, 0.035f, 0.045f, 1.0f) ;
            base.depth_s.do_change = true ; base.depth_s.ss.do_activate = false ;
            base.depth_s.ss.do_depth_write = true ;
            base.blend_s.do_change = true ; base.blend_s.ss.do_activate = false ;
            base.polygon_s.do_change = true ; base.polygon_s.ss.do_activate = true ;
            // Use a supported active cull mode; GL_NONE is not a glCullFace argument.
            base.polygon_s.ss.cm = g::cull_mode::back ;
            base.polygon_s.ss.ff = g::front_face::counter_clock_wise ;
            base.scissor_s.do_change = true ; base.scissor_s.ss.do_activate = false ;
            base.view_s.do_change = true ; base.view_s.ss.do_activate = true ;
            base.view_s.ss.vp = motor::math::vec4ui_t(0, 0, w.width, w.height) ;
            {
                s[less].depth_s.do_change = true ; s[less].depth_s.ss.do_activate = true ;
                s[equal] = s[less] ; s[equal].depth_s.ss.compare_funk = g::depth_compare::less_equal ;
                s[no_write] = s[less] ; s[no_write].depth_s.ss.do_depth_write = false ;
            }
            {
                s[alpha].blend_s.do_change = true ; s[alpha].blend_s.ss.do_activate = true ;
                s[alpha].blend_s.ss.src_blend_factor = g::blend_factor::src_alpha ;
                s[alpha].blend_s.ss.dst_blend_factor = g::blend_factor::one_minus_src_alpha ;
                s[additive] = s[alpha] ;
                s[additive].blend_s.ss.src_blend_factor = g::blend_factor::one ;
                s[additive].blend_s.ss.dst_blend_factor = g::blend_factor::one ;
            }
            {
                s[back].polygon_s.do_change = true ; s[back].polygon_s.ss.do_activate = true ;
                s[back].polygon_s.ss.cm = g::cull_mode::back ;
                s[back].polygon_s.ss.ff = g::front_face::counter_clock_wise ;
                s[front] = s[back] ; s[front].polygon_s.ss.cm = g::cull_mode::front ;
                s[wire] = s[back] ;
                s[wire].polygon_s.ss.fm = _alternate ? g::fill_mode::fill : g::fill_mode::line ;
            }
            {
                s[scissor].scissor_s.do_change = true ; s[scissor].scissor_s.ss.do_activate = true ;
                s[scissor].scissor_s.ss.rect = pixel_rect(w, cx(11)-0.06f, cy(11), 0.07f, 0.13f) ;
                s[viewport].view_s.do_change = true ; s[viewport].view_s.ss.do_activate = true ;
                s[viewport].view_s.ss.vp = pixel_rect(w, cx(7)-0.04f, cy(7), 0.12f, 0.12f) ;
            }
            w.states.resize(state_count) ;
            for( size_t i = 0 ; i < state_count ; ++i ) w.states.set_render_states_set(i, s[i]) ;
            w.dirty = false ;
        }
        static void_t pop( fe_t fe ) { fe->pop(g::gen4::backend::pop_type::render_state) ; }
        void_t render( fe_t fe, draw const & d )
        {
            g::gen4::backend_t::render_detail_t detail ; detail.geo = d.geo ; detail.varset = d.vs ;
            fe->render(_shader, detail) ;
        }
        template< typename T >
        bool_t ready( fe_t fe, T * object )
        {
            auto const status = fe->decode(object) ;
            if( status.first == g::object_state::in_transit ) return false ;
            if( status.first != g::object_state::ready || status.second != g::result::ok )
            { fail("[states] resource configure failed") ; return false ; }
            return true ;
        }
        void_t request( bool_t release )
        {
            _release_request = release ; ++_revision ;
        }

    public:
        render_states_app( options const & opts, bool_t * passed ) : _opts(opts), _passed(passed) {}
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            _quad = geometry(false) ; _reverse = geometry(true) ;
            {
                g::msl_object_t shader("states.shader") ;
                shader.add(g::msl_api_type::msl_4_0, R"(
                    config states.shader {
                        vertex_shader {
                            in vec3_t pos : position ; out vec4_t pos : position ;
                            vec4_t u_rect ; float_t u_depth ;
                            void main() {
                                out.pos = vec4_t((in.pos.xy ' u_rect.zw) + u_rect.xy, u_depth, 1.0) ;
                            }
                        }
                        pixel_shader {
                            out vec4_t color : color ; vec4_t u_color ;
                            void main() { out.color = u_color ; }
                        }
                    }
                )") ;
                _shader = motor::shared(std::move(shader)) ;
                _shader->link_geometry(_quad.name()) ; _shader->link_geometry(_reverse.name()) ;
            }
            {
                vec4_t const orange(0.9f, 0.32f, 0.12f, 0.5f), blue(0.1f, 0.4f, 0.8f, 1.0f) ;
                for( size_t i = 0 ; i < 12 ; ++i )
                {
                    float_t const x = cx(i), y = cy(i) ;
                    _background[i] = make_draw(vec4_t(x, y, 0.225f, 0.245f), 0.9f, vec4_t(0.065f, 0.08f, 0.095f, 1.0f)) ;
                    _first[i] = make_draw(vec4_t(x-0.04f, y+0.01f, 0.13f, 0.13f), 0.25f, i < 4 ? orange : blue) ;
                    _second[i] = make_draw(vec4_t(x+0.04f, y-0.04f, 0.13f, 0.13f), i == 2 ? 0.25f : 0.7f, i < 4 ? blue : orange) ;
                    _marker[i] = make_draw(vec4_t(x+0.17f, y-0.19f, 0.025f, 0.015f), 0.8f, vec4_t(0.2f, 0.95f, 0.7f, 1.0f)) ;
                    if( i == 8 || i == 9 )
                    {
                        _first[i] = make_draw(vec4_t(x-0.1f, y, 0.07f, 0.12f), 0.5f, blue) ;
                        _second[i] = make_draw(vec4_t(x+0.1f, y, 0.07f, 0.12f), 0.5f, orange, 1) ;
                    }
                }
                _viewport_draw = make_draw(vec4_t(0.0f, 0.0f, 0.85f, 0.85f), 0.5f, blue) ;
            }
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ;
                w.states = g::state_object_t("states.window." + motor::to_string(i)) ;
                motor::application::window_info_t info ;
                info.x = int_t(20 + i*820) ; info.y = 60 ; info.w = w.width ; info.h = w.height ;
                _opts.capture.apply( info ) ;
                w.width = uint_t( info.w ) ; w.height = uint_t( info.h ) ;
                bool_t const d3d = _opts.d3d || i == 1 ;
                info.window_name = d3d ? "motor | render states | d3d11" : "motor | render states | gl4" ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                w.id = create_window(info) ;
                send_window_message(w.id, [](auto & window)
                {
                    window.send_message(motor::application::show_message_t({true})) ;
                    window.send_message(motor::application::vsync_message_t({true})) ;
                }) ;
            }
        }
        void_t on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto & w = _windows[i] ;
                if( w.dirty || w.revision != _revision ) make_states(w) ;
            }
        }
        void_t on_first_frame( window_id_t id, fe_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            make_states(w) ;
            fe->configure<g::geometry_object_t>(&_quad) ; fe->configure<g::geometry_object_t>(&_reverse) ;
            fe->configure<g::msl_object_t>(_shader) ; fe->configure<g::state_object_t>(&w.states) ;
        }
        void_t on_render( window_id_t id, fe_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            if( _closing ) return ;
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            if( w.revision != _revision )
            {
                w.revision = _revision ; w.frames = 0 ;
                if( _release_request ) { fe->release<g::state_object_t>(&w.states) ; w.state = phase::release ; }
                else { fe->configure<g::state_object_t>(&w.states) ; w.state = phase::configure ; }
                return ;
            }
            if( w.state == phase::release )
            {
                auto const status = fe->decode(&w.states) ;
                if( status.first == g::object_state::in_transit ) return ;
                if( status.first != g::object_state::raw || status.second != g::result::ok )
                { fail("[states] release completed successfully") ; return ; }
                motor::log::global_t::test("[states] release returned raw / ok", true) ;
                fe->configure<g::state_object_t>(&w.states) ; w.state = phase::configure ; return ;
            }
            if( !ready(fe, &w.states) || !ready(fe, _shader) || !ready(fe, &_quad) || !ready(fe, &_reverse) ) return ;
            if( w.state == phase::configure )
            {
                motor::log::global_t::test("[states] configured state sets: " + motor::to_string(w.states.size()), w.states.size() == state_count) ;
                w.state = phase::active ; ++w.cycles ;
            }
            fe->push(&w.states, root) ;
            for( size_t i = 0 ; i < 12 ; ++i )
            {
                render(fe, _background[i]) ;
                if( i == 0 || i == 4 ) { render(fe, _first[i]) ; render(fe, _second[i]) ; }
                else if( i <= 2 )
                {
                    fe->push(&w.states, i == 1 ? less : equal) ;
                    render(fe, _first[i]) ; render(fe, _second[i]) ; pop(fe) ;
                }
                else if( i == 3 )
                {
                    fe->push(&w.states, less) ; fe->push(&w.states, no_write) ;
                    render(fe, _first[i]) ; pop(fe) ; render(fe, _second[i]) ; pop(fe) ;
                }
                else if( i == 5 || i == 6 )
                {
                    render(fe, _first[i]) ; fe->push(&w.states, i == 5 ? alpha : additive) ;
                    render(fe, _second[i]) ; pop(fe) ;
                }
                else if( i == 7 )
                {
                    fe->push(&w.states, viewport) ; render(fe, _viewport_draw) ; pop(fe) ;
                }
                else
                {
                    size_t const state = i == 8 ? back : i == 9 ? front : i == 10 ? wire : scissor ;
                    fe->push(&w.states, state) ; render(fe, _first[i]) ; render(fe, _second[i]) ; pop(fe) ;
                }
                render(fe, _marker[i]) ;
            }
            pop(fe) ; ++w.frames ;
        }
        bool_t on_tool( window_id_t id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            auto const & w = _windows[id == _windows[0].id ? 0 : 1] ;
            ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always) ;
            ImGui::SetNextWindowSize(ImVec2(float_t(w.width)-16.0f, 96.0f), ImGuiCond_Always) ;
            ImGui::Begin("Render states", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse) ;
            bool_t busy = _opts.smoke ;
            for( size_t i = 0 ; i < count() ; ++i ) busy = busy || _windows[i].state != phase::active || _windows[i].revision != _revision ;
            ImGui::BeginDisabled(busy) ;
            if( ImGui::Button("Reconfigure") ) request(false) ;
            ImGui::SameLine() ; if( ImGui::Button("Release / configure") ) request(true) ;
            ImGui::SameLine() ; if( ImGui::Checkbox("Solid instead of wire", &_alternate) ) request(false) ;
            ImGui::EndDisabled() ;
            ImGui::TextWrapped("Depth row: orange first (near), blue second (far); LESS_EQUAL uses equal depth.") ;
            ImGui::End() ;
            for( size_t i = 0 ; i < 12 ; ++i )
            {
                auto * const list = ImGui::GetForegroundDrawList() ;
                ImVec2 const position((cx(i)-0.20f+1.0f)*0.5f*float_t(w.width),
                    (1.0f-cy(i)-0.21f)*0.5f*float_t(w.height)) ;
                ImVec2 const label_size = ImGui::CalcTextSize(_labels[i]) ;
                ImVec2 const info_position(position.x + label_size.x + 6.0f, position.y) ;
                ImVec2 const info_size = ImGui::CalcTextSize("(?)") ;
                list->AddText(position, IM_COL32(230,235,240,255), _labels[i]) ;
                list->AddText(info_position, IM_COL32(150,175,195,255), "(?)") ;
                if( ImGui::IsMouseHoveringRect(position,
                    ImVec2(info_position.x + info_size.x, position.y + label_size.y), false) )
                {
                    auto const & d = _descriptions[i] ;
                    bool_t const solid = i == 10 && _alternate ;
                    float_t const available = float_t(w.width) - 32.0f ;
                    float_t const wrap_width = available < 340.0f ? (available > 1.0f ? available : 1.0f) : 340.0f ;
                    ImGui::BeginTooltip() ;
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_width) ;
                    ImGui::TextUnformatted(_labels[i]) ;
                    ImGui::Separator() ;
                    ImGui::TextWrapped("Draw order: %s", d.order) ;
                    ImGui::TextWrapped("Depth: %s", d.depth) ;
                    ImGui::TextWrapped("State: %s", solid ? "Solid fill enabled for both rectangles." : d.state) ;
                    ImGui::Separator() ;
                    ImGui::TextWrapped("Expected: %s", solid ? "Filled rectangles; orange covers blue in the overlap. The green marker is solid." : d.expected) ;
                    ImGui::TextWrapped("Green is drawn last with the base states; it must be solid and unclipped.") ;
                    ImGui::PopTextWrapPos() ;
                    ImGui::EndTooltip() ;
                }
            }
            return true ;
        }
        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _closing ) return ;
            for( size_t i = 0 ; i < count() ; ++i )
                if( _windows[i].revision != _revision || _windows[i].state != phase::active || _windows[i].frames < 30 ) return ;
            motor::log::global_t::test("[states] smoke phase " + motor::to_string(_step) + " submitted on every backend (pixels checked separately)", true) ;
            switch( _step++ )
            {
            case 0: _alternate = true ; request(false) ; break ;
            case 1: _alternate = false ; request(false) ; break ;
            case 2: request(true) ; break ;
            default:
                *_passed = !_failed ;
                for( size_t i = 0 ; i < count() ; ++i ) *_passed = *_passed && _windows[i].cycles == 4 ;
                motor::log::global_t::test("[states] all configure cycles completed", *_passed) ;
                _closing = true ; close() ; break ;
            }
        }
        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_closing && std::chrono::steady_clock::now()-_started > std::chrono::seconds(30) )
                fail("[states] smoke finished before timeout") ;
        }
        void_t on_event( window_id_t id, motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.close_changed ) { _closing = true ; close() ; }
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
                w.width = uint_t(event.resize_msg.w) ; w.height = uint_t(event.resize_msg.h) ; w.dirty = true ;
                request(false) ;
            }
        }
        void_t on_last_frame( window_id_t id, fe_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            auto & w = _windows[id == _windows[0].id ? 0 : 1] ;
            fe->release<g::msl_object_t>(_shader) ; fe->release<g::geometry_object_t>(&_quad) ;
            fe->release<g::geometry_object_t>(&_reverse) ; fe->release<g::state_object_t>(&w.states) ;
        }
        void_t on_shutdown( void_t ) noexcept override { motor::release(motor::move(_shader)) ; }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
#ifdef _WIN32
    opts.dual = true ;
#endif
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--cpu-only" ) opts.cpu = true ;
        else if( arg == "--youtube-short" && i + 1 < argc && opts.capture.parse( argv[i+1] ) ) ++i ;
        else
        {
            motor::log::global_t::status("Usage: 23_render_states [--gl-only|--d3d-only] [--smoke] [--cpu-only] [--youtube-short square|vertical]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.d3d ) { motor::log::global_t::error("d3d11 requires Windows") ; motor::log::global_t::deinit() ; return 2 ; }
#endif
    bool const cpu_ok = sample::cpu_checks() ;
    bool passed = false ; int result = 0 ;
    if( !opts.cpu && cpu_ok )
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::render_states_app(opts, &passed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::io::global_t::deinit() ;
    motor::profiling::global_t::deinit() ; motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return result != 0 ? result : (!cpu_ok || !clean || (!opts.cpu && opts.smoke && !passed) ? 1 : 0) ;
}
