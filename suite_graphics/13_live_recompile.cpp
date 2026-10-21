#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/string>
#include <atomic>
#include <chrono>
#include <cmath>
#include "../comparison/compile_cost.hpp"
#include <charconv>
#include <cstring>

namespace sample
{
    using namespace motor::core::types ;
    using frontend = motor::graphics::gen4::frontend_ptr_t ;
    using state = motor::graphics::object_state ;
    using result = motor::graphics::result ;
    using clock = std::chrono::steady_clock ;
    struct options
    {
        bool_t dual = false, d3d = false, smoke = false ;
        bool_t valid_only = false, close_on_submit = false ;
        bool_t benchmark = false ;
        uint32_t seed = 1 ;
    } ;

    motor::string_t source( motor::string_in_t name, int_t const variant )
    {
        motor::string_t code = "config " + name + R"(
        {
            vertex_shader
            {
                in vec3_t pos : position ;
                out vec4_t pos : position ;
                vec4_t u_offset ;
                void main() { out.pos = vec4_t( in.pos, 1.0 ) + u_offset ; }
            }
            pixel_shader
            {
                out vec4_t color : color ;
                vec4_t u_color ;
        )" ;
        if( variant == 1 ) code += "vec4_t u_tint ;\n" ;
        code += "void main() { out.color = " ;
        code += variant == 2 ? "u_deliberately_undefined" : variant == 1 ? "u_color * u_tint" : "u_color" ;
        return code + " ; } } }" ;
    }

    class recompile_app : public motor::application::app
    {
        struct window_data
        {
            window_id_t id = size_t( -1 ) ;
            char_cptr_t label = "GL4" ;
            size_t submitted = 0, completed = 0, draws = 0, pending_frames = 0 ;
            std::atomic_size_t executed { 0 }, heartbeat { 0 } ;
            bool_t started = false ;
            state shader_state = state::raw ;
            result shader_result = result::invalid ;
            window_data( void_t ) noexcept = default ;
            window_data( window_data const & ) = delete ;
            // Application construction moves these before any commands capture them.
            window_data( window_data && rhs ) noexcept : id( rhs.id ), label( rhs.label ),
                submitted( rhs.submitted ), completed( rhs.completed ), draws( rhs.draws ),
                pending_frames( rhs.pending_frames ), executed( rhs.executed.load() ), heartbeat( rhs.heartbeat.load() ),
                started( rhs.started ), shader_state( rhs.shader_state ), shader_result( rhs.shader_result )
            { assert( !started ) ; }
        } ;
        struct vertex { motor::math::vec3f_t pos ; } ;
        options _options ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::state_object_t _state ;
        motor::graphics::msl_object_mtr_t _shader = nullptr, _background = nullptr ;
        motor::graphics::data_variable<motor::math::vec4f_t> * _heartbeat_offset = nullptr ;
        size_t _geometry_id = 0, _background_geo = 0, _background_vars = 0 ;
        size_t _sets[12] = {} ;
        size_t _revision = 1, _step = 0 ;
        int_t _variant = 2, _requested = -1 ;
        bool_t _quitting = false, _failed = false ;
        clock::time_point _started, _changed ;
        clock::time_point _previous_frame, _compile_start ;
        compile_cost::measurements _measurements ;
        bool_t _compile_active = false, _active_this_frame = false ;
        size_t _idle_frames = 0, _bench_round = 0, _heartbeat_start = 0 ;

        motor::string_t current_source( void_t ) const
        {
            if( !_options.benchmark || _revision == 1 ) return source( "live_shader", _variant ) ;
            return "config live_shader { vertex_shader { in vec3_t pos : position ; out vec4_t pos : position ; "
                "vec4_t u_offset ; void main() { out.pos = vec4_t( in.pos, 1.0 ) + u_offset ; } } "
                "pixel_shader { out vec4_t color : color ; vec4_t u_color ; void main() { " +
                compile_cost::body( _options.seed, _revision - 1, true ) + " } } }" ;
        }

        size_t count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }
        window_data * find( window_id_t const id ) noexcept
        {
            for( size_t i = 0 ; i < count() ; ++i ) if( _windows[i].id == id ) return &_windows[i] ;
            return nullptr ;
        }
        bool_t settled( void_t ) const noexcept
        {
            for( size_t i = 0 ; i < count() ; ++i )
                if( _windows[i].completed != _revision || _windows[i].executed.load() < 24 ) return false ;
            return true ;
        }
        void_t fail( motor::string_in_t message ) noexcept
        {
            motor::log::global_t::test( "[live-recompile] " + message, false ) ;
            _failed = true ; _quitting = true ; close() ;
        }

    public:
        recompile_app( options const opts, bool_t * passed ) noexcept : _options( opts ), _passed( passed )
        {
            if( opts.valid_only || opts.close_on_submit || opts.benchmark ) _variant = 0 ;
        }

        void_t on_init( void_t ) noexcept override
        {
            if( _options.benchmark ) motor::log::global_t::status( "[compile-cost] motor seed=" +
                motor::to_string( _options.seed ) + " operations=" + motor::to_string( compile_cost::operations ) ) ;
            _started = _changed = clock::now() ;
            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .resize( 4 ).update<vertex>( []( vertex * p, size_t )
                    {
                        p[0].pos = motor::math::vec3f_t( -0.12f, -0.12f, 0.5f ) ;
                        p[1].pos = motor::math::vec3f_t(  0.12f, -0.12f, 0.5f ) ;
                        p[2].pos = motor::math::vec3f_t(  0.12f,  0.12f, 0.5f ) ;
                        p[3].pos = motor::math::vec3f_t( -0.12f,  0.12f, 0.5f ) ;
                    } ) ;
                auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint )
                    .resize( 6 ).update<uint_t>( []( uint_t * p, size_t )
                    { uint_t const indices[] = { 0, 1, 2, 0, 2, 3 } ; for( size_t i = 0 ; i < 6 ; ++i ) p[i] = indices[i] ; } ) ;
                _geometry = motor::graphics::geometry_object_t( "live_quad",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                _shader = motor::shared( motor::graphics::msl_object_t( "live_shader" ) ) ;
                _shader->add( motor::graphics::msl_api_type::msl_4_0, current_source() ) ;
                _geometry_id = _shader->link_geometry( "live_quad" ) ;
                for( size_t i = 0 ; i < 12 ; ++i )
                {
                    auto vars = motor::shared( motor::graphics::variable_set_t() ) ;
                    vars->data_variable<motor::math::vec4f_t>( "u_offset" )->set( motor::math::vec4f_t(
                        -0.6f + float_t( i % 4 ) * 0.4f, -0.35f + float_t( i / 4 ) * 0.36f, 0.0f, 0.0f ) ) ;
                    vars->data_variable<motor::math::vec4f_t>( "u_color" )->set(
                        motor::math::vec4f_t( 0.15f + float_t( i % 4 ) * 0.2f, 0.75f, 0.9f, 1.0f ) ) ;
                    // Only B consumes this variable. A -> B -> A changes the GPU layout.
                    vars->data_variable<motor::math::vec4f_t>( "u_tint" )->set( motor::math::vec4f_t( 1.0f, 0.35f, 0.2f, 1.0f ) ) ;
                    _sets[i] = _shader->add_variable_set( motor::move( vars ) ) ;
                }
                _background = motor::shared( motor::graphics::msl_object_t( "live_heartbeat" ) ) ;
                _background->add( motor::graphics::msl_api_type::msl_4_0, source( "live_heartbeat", 0 ) ) ;
                _background_geo = _background->link_geometry( "live_quad" ) ;
                auto vars = motor::shared( motor::graphics::variable_set_t() ) ;
                _heartbeat_offset = vars->data_variable<motor::math::vec4f_t>( "u_offset" ) ;
                _heartbeat_offset->set( motor::math::vec4f_t( 0.0f, -0.8f, 0.0f, 0.0f ) ) ;
                vars->data_variable<motor::math::vec4f_t>( "u_color" )->set( motor::math::vec4f_t( 0.25f, 0.9f, 0.4f, 1.0f ) ) ;
                _background_vars = _background->add_variable_set( motor::move( vars ) ) ;
            }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ; states.depth_s.ss.do_activate = false ;
                states.depth_s.ss.do_depth_write = false ;
                states.polygon_s.do_change = true ; states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ; states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ; states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.035f, 0.045f, 0.055f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "live_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            for( size_t i = 0 ; i < count() ; ++i )
            {
                bool_t const d3d = _options.d3d || i == 1 ;
                auto & w = _windows[i] ; w.label = d3d ? "D3D11" : "GL4" ;
                motor::application::window_info_t info ;
                info.window_name = motor::string_t( "motor | live recompile | " ) + w.label ;
                info.x = int_t( 40 + i * 660 ) ; info.y = 80 ; info.w = 640 ; info.h = 640 ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                w.id = create_window( info ) ;
                send_window_message( w.id, [vsync = !_options.benchmark]( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { vsync } ) ) ;
                } ) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            if( _quitting ) return ;
            // This set belongs to the unchanged shader, never to the compilation in flight.
            bool_t background_ready = true ;
            for( size_t i = 0 ; i < count() ; ++i ) background_ready &= _windows[i].started ;
            if( background_ready )
            {
                auto const t = std::chrono::duration<float_t>( clock::now() - _started ).count() ;
                _heartbeat_offset->set( motor::math::vec4f_t( std::sin( t ) * 0.7f, -0.8f, 0.0f, 0.0f ) ) ;
            }
            if( _requested < 0 || !settled() ) return ;
            _variant = _requested ; _requested = -1 ; ++_revision ; _changed = clock::now() ;
            _shader->clear_shaders().add( motor::graphics::msl_api_type::msl_4_0, current_source() ) ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                _windows[i].draws = 0 ; _windows[i].executed.store( 0 ) ; _windows[i].pending_frames = 0 ;
            }
            motor::log::global_t::status( "[live-recompile] revision " + motor::to_string( _revision ) +
                " variant " + motor::to_string( _variant ) ) ;
        }

        void_t on_first_frame( window_id_t const, frontend fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::state_object_t>( &_state ) ;
            fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->configure<motor::graphics::msl_object_t>( _background ) ;
        }

        void_t on_render( window_id_t const id, frontend fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto * w = find( id ) ; if( !w || _quitting ) return ;
            auto ready = [fe]( motor::graphics::object_mtr_t object )
            {
                auto const s = fe->decode( object ) ; return s.first == state::ready && s.second == result::ok ;
            } ;
            if( !ready( &_state ) || !ready( &_geometry ) || !ready( _background ) ) return ;
            w->started = true ;
            if( w->submitted != _revision )
            {
                if( _options.benchmark && _revision > 1 )
                {
                    _compile_start = clock::now() ; _compile_active = true ; _active_this_frame = true ;
                    _heartbeat_start = w->heartbeat.load() ;
                }
                if( !fe->configure<motor::graphics::msl_object_t>( _shader ) ) { fail( "configure rejected" ) ; return ; }
                w->submitted = _revision ;
            }
            auto const status = fe->decode( _shader ) ;
            w->shader_state = status.first ; w->shader_result = status.second ;
            bool_t const pending = status.first == state::in_transit ;
            if( pending ) ++w->pending_frames ;
            else if( w->completed != _revision )
            {
                bool_t const valid = status.first == state::ready && status.second == result::ok ;
                if( _options.benchmark )
                {
                    auto * listener = _shader->borrow_compilation_listener() ;
                    motor::graphics::shader_bindings_t bindings ;
                    if( !listener->has_changed() || !listener->reset_and_successful( bindings ) )
                    { fail( "benchmark revision did not report successful compilation" ) ; return ; }
                }
                bool_t const rejected = status.first == state::raw && status.second == result::failed ;
                // A fresh invalid shader must fail. After success, object intentionally
                // preserves ready/ok for the last valid resource, not the last command.
                bool_t const expected = _variant == 2 ? rejected || (_revision > 1 && valid) : valid ;
                motor::log::global_t::test( motor::string_t( "[live-recompile][" ) + w->label +
                    "] revision " + motor::to_string( _revision ) + (_variant == 2 ?
                        (_revision == 1 ? " rejected initial invalid shader" : " invalid recompile settled (resource status only)") : " compiled" ), expected ) ;
                if( !expected )
                {
                    motor::log::global_t::error( "[live-recompile] unexpected state/result: " +
                        motor::to_string( int_t( status.first ) ) + "/" + motor::to_string( int_t( status.second ) ) ) ;
                    _failed = true ;
                    // Keep the negative result, but still exercise recovery after a
                    // misreported invalid compile. Never draw the invalid variant.
                    if( _variant != 2 || (status.first != state::raw && status.first != state::ready) )
                    { fail( "unexpected compilation result" ) ; return ; }
                }
                w->completed = _revision ;
                if( _options.benchmark && _compile_active )
                {
                    auto const latency = compile_cost::ms( _compile_start, clock::now() ) ;
                    _measurements.latency.emplace_back( latency ) ;
                    auto const progress = w->heartbeat.load() - _heartbeat_start ;
                    _measurements.progress_frames += progress ;
                    motor::log::global_t::status( "[compile-cost] motor revision=" + motor::to_string( _revision - 1 ) +
                        " ready_ms=" + motor::to_string( latency ) + " command_progress=" + motor::to_string( progress ) ) ;
                    _compile_active = false ; _active_this_frame = true ; _idle_frames = 0 ;
                }
                motor::log::global_t::status( "  pending frames=" + motor::to_string( w->pending_frames ) ) ;
            }
            fe->push( &_state ) ;
            motor::graphics::gen4::backend_t::render_detail_t detail ;
            detail.geo = _background_geo ; detail.varset = _background_vars ;
            fe->render( _background, detail ) ;
            fe->fence( [w]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase == motor::graphics::gen4::frontend::fence_phase::execute )
                    w->heartbeat.fetch_add( 1 ) ;
            } ) ;
            if( !pending && _variant != 2 && (!_options.benchmark || w->draws == 0) )
            {
                detail.geo = _geometry_id ;
                // Benchmark keeps twelve cheap quads visible during compilation.
                // The compiled program is exercised once after delivery.
                for( size_t i = _options.benchmark ? 1 : 12 ; i-- > 0 ; ) { detail.varset = _sets[i] ; fe->render( _shader, detail ) ; }
                ++w->draws ;
            }
            if( _options.benchmark )
            {
                // Fixed cheap draw workload, independent of the compiled program.
                detail.geo = _background_geo ; detail.varset = _background_vars ;
                for( size_t i = 0 ; i < 12 ; ++i ) fe->render( _background, detail ) ;
            }
            if( !pending ) fe->fence( [w]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase == motor::graphics::gen4::frontend::fence_phase::execute )
                    w->executed.fetch_add( 1 ) ;
            } ) ;
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( _options.benchmark && !_quitting )
            {
                auto const now = clock::now() ;
                if( _windows[0].started && _previous_frame != clock::time_point{} )
                {
                    auto & samples = _compile_active || _active_this_frame ? _measurements.active : _measurements.idle ;
                    if( _compile_active || _active_this_frame || _idle_frames >= 30 )
                        samples.emplace_back( compile_cost::ms( _previous_frame, now ) ) ;
                }
                _previous_frame = now ; _active_this_frame = false ;
                if( !settled() ) return ;
                if( ++_idle_frames < compile_cost::idle_frames ) return ;
                if( _bench_round < compile_cost::rounds )
                { ++_bench_round ; _requested = 0 ; _idle_frames = 0 ; }
                else
                {
                    _measurements.report( _windows[0].label ) ;
                    *_passed = !_failed && _measurements.latency.size() == compile_cost::rounds ;
                    motor::log::global_t::test( "[compile-cost] all revisions delivered and drawn", *_passed ) ;
                    _quitting = true ; close() ;
                }
                return ;
            }
            if( _options.close_on_submit && !_quitting )
            {
                bool_t submitted = true ;
                for( size_t i = 0 ; i < count() ; ++i ) submitted &= _windows[i].submitted == _revision ;
                if( submitted )
                {
                    // This tests teardown after enqueue, not guaranteed compiler overlap.
                    motor::log::global_t::test( "[live-recompile] closing after configure submission", true ) ;
                    *_passed = !_failed ; _quitting = true ; close() ;
                }
                return ;
            }
            if( !_options.smoke || _quitting || !settled() ) return ;
            int_t const sequence[] = { 0, 1, 0, 1, 2, 0, 1, 0 } ;
            if( _step < sizeof( sequence ) / sizeof( sequence[0] ) )
            {
                _requested = sequence[_step++] ;
                if( _options.valid_only && _requested == 2 ) _requested = 0 ;
            }
            else
            {
                bool_t recovered = true ;
                for( size_t i = 0 ; i < count() ; ++i ) recovered &= _windows[i].draws >= 24 && _windows[i].heartbeat.load() >= 24 ;
                motor::log::global_t::test( "[live-recompile] final valid revision rendered after recovery", recovered ) ;
                bool_t const ok = !_failed && recovered ;
                *_passed = ok ;
                motor::log::global_t::test( _options.valid_only ?
                    "[live-recompile] valid A/B layouts; pixels require visual verification" :
                    "[live-recompile] A/B layouts, failure and recovery; pixels require visual verification", ok ) ;
                _quitting = true ; close() ;
            }
        }
        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( !_quitting && (!settled() || _options.smoke) && clock::now() - _changed > std::chrono::seconds( 20 ) ) fail( "operation timeout" ) ;
        }
        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting || _options.benchmark ) return false ;
            if( ImGui::Begin( "Live shader", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
            {
                ImGui::Text( "Revision %llu / variant %d", static_cast<unsigned long long>( _revision ), _variant ) ;
                ImGui::BeginDisabled( _options.smoke || !settled() || _requested >= 0 ) ;
                if( ImGui::Button( "A" ) ) _requested = 0 ; ImGui::SameLine() ;
                if( ImGui::Button( "B + tint" ) ) _requested = 1 ; ImGui::SameLine() ;
                if( ImGui::Button( "Invalid" ) ) _requested = 2 ;
                ImGui::EndDisabled() ;
                for( size_t i = 0 ; i < count() ; ++i ) ImGui::Text( "%s: pending=%llu, draws=%llu", _windows[i].label,
                    static_cast<unsigned long long>( _windows[i].pending_frames ), static_cast<unsigned long long>( _windows[i].draws ) ) ;
            }
            ImGui::End() ; return true ;
        }
        void_t on_last_frame( window_id_t const, frontend fe, motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>( _shader ) ;
            fe->release<motor::graphics::msl_object_t>( _background ) ;
            fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
            fe->release<motor::graphics::state_object_t>( &_state ) ;
        }
        void_t on_event( window_id_t const, motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        { if( event.close_changed ) { _quitting = true ; close() ; } }
        void_t on_shutdown( void_t ) noexcept override
        {
            _heartbeat_offset = nullptr ;
            motor::release( motor::move( _shader ) ) ; motor::release( motor::move( _background ) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg( argv[i] ) ;
        if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--benchmark" ) { opts.benchmark = true ; opts.smoke = true ; }
        else if( arg == "--seed" && i + 1 < argc )
        {
            auto const text = argv[++i] ; auto const end = text + std::strlen( text ) ;
            auto const parsed = std::from_chars( text, end, opts.seed ) ;
            if( parsed.ec != std::errc{} || parsed.ptr != end ) return 2 ;
        }
        else if( arg == "--valid-only" ) opts.valid_only = true ;
        else if( arg == "--close-on-submit" ) { opts.close_on_submit = true ; opts.smoke = true ; }
        else if( arg == "--dual" ) { opts.dual = true ; opts.d3d = false ; }
        else if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else
        {
            motor::log::global_t::status( "Usage: 13_live_recompile [--smoke] [--valid-only] [--close-on-submit] [--benchmark --seed N] [--gl-only|--d3d-only|--dual]" ) ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
    if( opts.benchmark && (opts.dual || opts.close_on_submit) ) return 2 ;
#ifndef _WIN32
    if( opts.dual || opts.d3d ) return 2 ;
#endif
    bool passed = false ;
    auto carrier = motor::platform::global_t::create_carrier( motor::shared( sample::recompile_app( opts, &passed ) ) ) ;
    int const code = carrier->exec() ; motor::release( motor::move( carrier ) ) ;
    motor::concurrent::global_t::deinit() ; motor::log::global_t::deinit() ;
    motor::profiling::global_t::deinit() ; motor::io::global_t::deinit() ;
    return code != 0 ? code : motor::memory::global_t::dump_to_std() != 0 || (opts.smoke && !passed) ? 1 : 0 ;
}
