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
#include <chrono>

namespace sample
{
    using namespace motor::core::types ;
    using state = motor::graphics::object_state ;
    using result = motor::graphics::result ;
    using status = motor::graphics::object_t::data_manipulator::state_pair_t ;
    using frontend = motor::graphics::gen4::frontend_ptr_t ;

    struct options
    {
        bool_t dual = true ;
        bool_t d3d = false ;
        bool_t smoke = false ;
        bool_t release_only = false ;
        bool_t reconfigure_only = false ;
    } ;

    char_cptr_t name( state const value ) noexcept
    {
        switch( value )
        {
        case state::raw: return "raw" ;
        case state::in_transit: return "in_transit" ;
        case state::ready: return "ready" ;
        default: return "invalid" ;
        }
    }

    char_cptr_t name( result const value ) noexcept
    {
        switch( value )
        {
        case result::ok: return "ok" ;
        case result::failed: return "failed" ;
        case result::invalid_argument: return "invalid_argument" ;
        case result::in_transit: return "in_transit" ;
        default: return "invalid" ;
        }
    }

    class reconfigure_app : public motor::application::app
    {
        enum class phase { idle, releasing, configuring, failed } ;
        struct window_data
        {
            window_id_t id = size_t( -1 ) ;
            char_cptr_t label = "GL4" ;
            phase current = phase::idle ;
            size_t seen = 0 ;
            size_t completed = 0 ;
            size_t draws = 0 ;
            status states[3] = {
                { state::invalid, result::invalid },
                { state::invalid, result::invalid },
                { state::invalid, result::invalid } } ;
            std::chrono::steady_clock::time_point started ;
        } ;
        struct vertex { motor::math::vec3f_t position ; } ;

        options _options ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::state_object_t _state ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        size_t _geo_id = size_t( -1 ) ;
        size_t _var_id = size_t( -1 ) ;
        window_data _windows[2] ;
        size_t _generation = 1 ;
        bool_t _release_first = false ;
        int_t _scope = 0 ;
        int_t _request_scope = 0 ;
        size_t _stable_frames = 0 ;
        size_t _smoke_step = 0 ;
        bool_t _quitting = false ;
        std::chrono::steady_clock::time_point _started ;

        size_t count( void_t ) const noexcept { return _options.dual ? 2 : 1 ; }

        void_t log( window_data const & w, char_cptr_t message ) noexcept
        {
            motor::log::global_t::status( motor::string_t( "[reconfigure][" ) + w.label + "] " + message ) ;
        }

        void_t snapshot( window_data & w, frontend fe ) noexcept
        {
            w.states[0] = fe->decode( &_state ) ;
            w.states[1] = fe->decode( &_geometry ) ;
            w.states[2] = fe->decode( _shader ) ;
        }

        void_t report( window_data const & w ) noexcept
        {
            char_cptr_t const labels[3] = { "state", "geometry", "msl" } ;
            for( size_t i = 0 ; i < 3 ; ++i )
            {
                motor::log::global_t::status( motor::string_t( "[reconfigure][" ) + w.label + "] " +
                    labels[i] + ": " + name( w.states[i].first ) + " / " + name( w.states[i].second ) ) ;
            }
        }

        bool_t busy( void_t ) const noexcept
        {
            for( size_t i = 0 ; i < count() ; ++i )
            {
                auto const & w = _windows[i] ;
                if( w.seen != _generation || w.current == phase::releasing ||
                    w.current == phase::configuring ) return true ;
                for( auto const & s : w.states ) if( s.first == state::in_transit ) return true ;
            }
            return false ;
        }

        void_t request( bool_t const release_first ) noexcept
        {
            _release_first = release_first ;
            _request_scope = _scope ;
            ++_generation ;
            _stable_frames = 0 ;
        }

        void_t configure( window_data & w, frontend fe ) noexcept
        {
            // Queue geometry before MSL: shader input binding needs the geometry.
            bool_t const a = fe->configure<motor::graphics::state_object_t>( &_state ) ;
            bool_t const b = fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
            bool_t const c = fe->configure<motor::graphics::msl_object_t>( _shader ) ;
            w.current = a && b && c ? phase::configuring : phase::failed ;
            w.started = std::chrono::steady_clock::now() ;
            log( w, a && b && c ? "configure queued" : "configure enqueue rejected" ) ;
        }

        void_t release( window_data & w, frontend fe ) noexcept
        {
            // Keep all CPU objects alive; this tests backend release, not ownership transfer.
            bool_t const a = fe->release<motor::graphics::msl_object_t>( _shader ) ;
            bool_t const b = fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
            bool_t const c = fe->release<motor::graphics::state_object_t>( &_state ) ;
            w.current = a && b && c ? phase::releasing : phase::failed ;
            w.started = std::chrono::steady_clock::now() ;
            log( w, a && b && c ? "release queued" : "release enqueue rejected" ) ;
        }

        void_t advance( window_data & w, frontend fe ) noexcept
        {
            if( w.current != phase::releasing && w.current != phase::configuring ) return ;
            bool_t waiting = false ;
            for( auto const & s : w.states ) waiting |= s.first == state::in_transit ;
            if( waiting )
            {
                if( std::chrono::steady_clock::now() - w.started > std::chrono::seconds( 15 ) )
                {
                    w.current = phase::failed ;
                    log( w, "operation timed out" ) ;
                    report( w ) ;
                }
                return ;
            }
            state const expected = w.current == phase::releasing ? state::raw : state::ready ;
            for( auto const & s : w.states )
            {
                if( s.first != expected || s.second != result::ok )
                {
                    log( w, w.current == phase::releasing ? "release FAILED; configure not queued" : "configure FAILED" ) ;
                    w.current = phase::failed ;
                    report( w ) ;
                    return ;
                }
            }
            if( w.current == phase::releasing )
            {
                log( w, "release completed; now configure" ) ;
                configure( w, fe ) ;
            }
            else
            {
                w.current = phase::idle ;
                ++w.completed ;
                w.draws = 0 ;
                log( w, "configure completed; rendering resumed" ) ;
                report( w ) ;
            }
        }

    public:
        reconfigure_app( options const opts, bool_t * passed ) noexcept
            : _options( opts ), _passed( passed ) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            {
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .resize( 3 ).update<vertex>( []( vertex * data, size_t )
                    {
                        data[0].position = motor::math::vec3f_t( -0.65f, -0.80f, 0.5f ) ;
                        data[1].position = motor::math::vec3f_t( 0.0f, 0.0f, 0.5f ) ;
                        data[2].position = motor::math::vec3f_t( 0.65f, -0.80f, 0.5f ) ;
                    } ) ;
                auto ib = motor::graphics::index_buffer_t()
                    .set_layout_element( motor::graphics::type::tuint )
                    .resize( 3 ).update<uint_t>( []( uint_t * data, size_t )
                    { data[0] = 0 ; data[1] = 1 ; data[2] = 2 ; } ) ;
                _geometry = motor::graphics::geometry_object_t( "reconfigure_triangle",
                    motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
            }
            {
                motor::graphics::msl_object_t shader( "reconfigure_shader" ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, R"(
                    config reconfigure_shader
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ;
                            out vec4_t pos : position ;
                            void main() { out.pos = vec4_t( in.pos, 1.0 ) ; }
                        }
                        pixel_shader
                        {
                            out vec4_t color : color ;
                            vec4_t u_color ;
                            void main() { out.color = u_color ; }
                        }
                    }
                )" ) ;
                _geo_id = shader.link_geometry( "reconfigure_triangle" ) ;
                auto vars = motor::shared( motor::graphics::variable_set_t() ) ;
                vars->data_variable<motor::math::vec4f_t>( "u_color" )->set(
                    motor::math::vec4f_t( 0.15f, 0.75f, 0.45f, 1.0f ) ) ;
                _var_id = shader.add_variable_set( motor::move( vars ) ) ;
                _shader = motor::shared( std::move( shader ) ) ;
            }
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = false ;
                states.depth_s.ss.do_depth_write = false ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.04f, 0.05f, 0.06f, 1.0f ) ;
                _state = motor::graphics::state_object_t( "reconfigure_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            for( size_t i = 0 ; i < count() ; ++i )
            {
                bool_t const d3d = _options.d3d || i == 1 ;
                _windows[i].label = d3d ? "D3D11" : "GL4" ;
                motor::application::window_info_t info ;
                info.window_name = motor::string_t( "motor | reconfigure | " ) + _windows[i].label ;
                info.x = int_t( 40 + i * 660 ) ; info.y = 90 ; info.w = 640 ; info.h = 640 ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                    motor::application::graphics_generation::gen4_gl4 ;
                _windows[i].id = create_window( info ) ;
                send_window_message( _windows[i].id, []( auto & window )
                {
                    window.send_message( motor::application::show_message( { true } ) ) ;
                    window.send_message( motor::application::vsync_message_t( { true } ) ) ;
                } ) ;
            }
        }

        void_t on_render( window_id_t const id, frontend fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            size_t i = 0 ;
            while( i < count() && _windows[i].id != id ) ++i ;
            if( i == count() ) return ;
            auto & w = _windows[i] ;
            if( data.last_frame )
            {
                release( w, fe ) ;
                return ;
            }
            snapshot( w, fe ) ;
            if( w.seen != _generation )
            {
                w.seen = _generation ;
                if( _request_scope == 0 || size_t( _request_scope - 1 ) == i )
                {
                    if( _release_first ) release( w, fe ) ;
                    else configure( w, fe ) ;
                    snapshot( w, fe ) ;
                    return ;
                }
            }
            advance( w, fe ) ;
            snapshot( w, fe ) ;
            if( w.current != phase::idle ) return ;
            fe->push( &_state ) ;
            {
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.geo = _geo_id ; detail.varset = _var_id ;
                fe->render( _shader, detail ) ;
                ++w.draws ;
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos( ImVec2( 10, 10 ), ImGuiCond_FirstUseEver ) ;
            if( ImGui::Begin( "Configure / Release", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings ) )
            {
                ImGui::BeginDisabled( busy() || _options.smoke ) ;
                if( _options.dual ) ImGui::Combo( "Backend", &_scope, "Both\0GL4\0D3D11\0" ) ;
                if( ImGui::Button( "Configure again" ) ) request( false ) ;
                ImGui::SameLine() ;
                if( ImGui::Button( "Release -> Configure" ) ) request( true ) ;
                ImGui::EndDisabled() ;
                for( size_t i = 0 ; i < count() ; ++i )
                {
                    auto const & w = _windows[i] ;
                    ImGui::Separator() ;
                    ImGui::Text( "%s | completed: %llu | %s", w.label,
                        static_cast<unsigned long long>( w.completed ),
                        w.current == phase::failed ? "FAILED" : ( w.current == phase::idle ? "idle" : "pending" ) ) ;
                    char_cptr_t const labels[3] = { "State", "Geometry", "MSL" } ;
                    for( size_t j = 0 ; j < 3 ; ++j )
                        ImGui::Text( "%s: %s / %s", labels[j], name( w.states[j].first ), name( w.states[j].second ) ) ;
                }
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_options.smoke || _quitting ) return ;
            for( size_t i = 0 ; i < count() ; ++i )
            {
                if( _windows[i].current == phase::failed ) { _quitting = true ; close() ; return ; }
                if( _windows[i].seen != _generation || _windows[i].current != phase::idle ||
                    _windows[i].draws < 30 ) return ;
            }
            if( ++_stable_frames < 30 ) return ;
            size_t const steps = _options.release_only || _options.reconfigure_only ? 2 : 4 ;
            if( _smoke_step < steps )
            {
                request( _options.release_only || ( !_options.reconfigure_only && _smoke_step >= 2 ) ) ;
                ++_smoke_step ;
            }
            else
            {
                *_passed = true ; _quitting = true ;
                motor::log::global_t::status( "[reconfigure] smoke completed; pixel verification is separate" ) ;
                close() ;
            }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _options.smoke && !_quitting &&
                std::chrono::steady_clock::now() - _started > std::chrono::seconds( 60 ) )
            {
                motor::log::global_t::error( "[reconfigure] smoke timeout" ) ;
                _quitting = true ; close() ;
            }
        }

        void_t on_event( window_id_t const,
            motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override { motor::release( motor::move( _shader ) ) ; }
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
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--reconfigure-only" ) opts.reconfigure_only = true ;
        else if( arg == "--release-only" ) opts.release_only = true ;
        else
        {
            motor::log::global_t::status( "Usage: 04_reconfigure [--gl-only|--d3d-only] [--smoke] [--reconfigure-only|--release-only]" ) ;
            motor::log::global_t::deinit() ;
            return arg == "--help" ? 0 : 2 ;
        }
    }
    if( opts.release_only && opts.reconfigure_only ) return 2 ;
#ifndef _WIN32
    if( opts.d3d ) return 2 ;
#endif
    bool passed = false ;
    int code = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared( sample::reconfigure_app( opts, &passed ) ) ) ;
        code = carrier->exec() ;
        motor::release( motor::move( carrier ) ) ;
    }
    motor::concurrent::global::deinit() ;
    motor::log::global::deinit() ;
    motor::profiling::global::deinit() ;
    motor::io::global::deinit() ;
    auto const remaining = motor::memory::global::dump_to_std() ;
    return code != 0 ? code : ( remaining != 0 || ( opts.smoke && !passed ) ? 1 : 0 ) ;
}
