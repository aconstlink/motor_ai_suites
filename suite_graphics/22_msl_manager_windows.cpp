#include <motor/platform/global.h>
#include <motor/gfx/manager/msl_manager.h>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/concurrent/global.h>
#include <motor/io/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

namespace callback_windows
{
    using namespace motor::core::types;
    using manager_t = motor::gfx::msl_manager_t;
    using msl_ptr_t = motor::graphics::msl_object_mtr_t;
    using frontend_t = motor::graphics::gen4::frontend_t;

    class app : public motor::application::app
    {
        static constexpr window_id_t invalid = window_id_t( -1 );
        struct window_data
        {
            window_id_t id = invalid;
            size_t frames = 0;
            bool_t initialized = false;
            bool_t last_frame = false;
        };
        struct completion
        {
            std::atomic_bool released[3] = {};
        };

        bool_t _d3d;
        bool_t * _passed;
        bool_t _ok = true, _quitting = false;
        window_data _windows[3];
        std::shared_ptr< completion > _completion = std::make_shared< completion >();
        std::unique_ptr< manager_t > _manager;
        std::shared_ptr< std::promise< msl_ptr_t > > _created =
            std::make_shared< std::promise< msl_ptr_t > >();
        std::future< msl_ptr_t > _future;
        msl_ptr_t _shader = nullptr;
        motor::graphics::geometry_object_t _geometry;
        motor::graphics::state_object_t _state;
        size_t _early = size_t( -1 ), _late = size_t( -1 );
        size_t _early_calls = 0, _late_calls = 0;
        size_t _step = 0, _main_at_close = 0;
        std::chrono::steady_clock::time_point _started;

        void_t check( char const * text, bool_t const passed )
        {
            motor::log::global_t::test( text, passed );
            _ok = passed && _ok;
        }

        size_t slot( window_id_t const id ) const
        {
            for( size_t i = 0; i < 3; ++i ) if( _windows[i].id == id ) return i;
            return size_t( -1 );
        }

        void_t open( size_t const index )
        {
            motor::application::window_info_t info;
            info.window_name = "motor | msl callbacks | " + motor::string_t( _d3d ? "D3D11 | " : "GL4 | " ) +
                ( index == 0 ? "primary" : index == 1 ? "secondary" : "secondary reopened" );
            info.x = index == 0 ? 60 : 740;
            info.y = 100;
            info.w = 640;
            info.h = 480;
            info.gen = _d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4;
            _windows[index].id = create_window( info );
            send_window_message( _windows[index].id, []( auto & window )
            {
                window.send_message( motor::application::show_message( { true } ) );
                window.send_message( motor::application::vsync_message_t( { true } ) );
            } );
        }

        void_t close_secondary( size_t const index )
        {
            _main_at_close = _windows[0].frames;
            send_window_message( _windows[index].id, []( auto & window )
            {
                window.send_message( motor::application::close_message( { true } ) );
            } );
        }

    public:
        app( bool_t const d3d, bool_t * passed ) : _d3d( d3d ), _passed( passed ) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now();
            {
                struct vertex { motor::math::vec3f_t position; };
                auto vertices = motor::graphics::vertex_buffer_t()
                    .add_layout_element( motor::graphics::vertex_attribute::position,
                        motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                    .resize( 3 ).update< vertex >( []( vertex * data, size_t )
                    {
                        data[0].position = motor::math::vec3f_t( -0.75f, -0.65f, 0.5f );
                        data[1].position = motor::math::vec3f_t( 0.0f, 0.75f, 0.5f );
                        data[2].position = motor::math::vec3f_t( 0.75f, -0.65f, 0.5f );
                    } );
                auto indices = motor::graphics::index_buffer_t()
                    .set_layout_element( motor::graphics::type::tuint )
                    .resize( 3 ).update< uint_t >( []( uint_t * data, size_t )
                    { data[0] = 0; data[1] = 1; data[2] = 2; } );
                _geometry = motor::graphics::geometry_object_t( "callbacks.triangle",
                    motor::graphics::primitive_type::triangles, std::move( vertices ), std::move( indices ) );
            }
            {
                motor::graphics::render_state_sets_t states;
                states.depth_s.do_change = true;
                states.depth_s.ss.do_activate = false;
                states.depth_s.ss.do_depth_write = false;
                states.polygon_s.do_change = true;
                states.polygon_s.ss.do_activate = false;
                states.clear_s.do_change = true;
                states.clear_s.ss.do_activate = true;
                states.clear_s.ss.do_color_clear = true;
                states.clear_s.ss.do_depth_clear = true;
                states.clear_s.ss.clear_color = motor::math::vec4f_t( 0.04f, 0.05f, 0.07f, 1.0f );
                _state = motor::graphics::state_object_t( "callbacks.state" );
                _state.add_render_state_set( states );
            }
            {
                _manager = std::make_unique< manager_t >( motor::shared( motor::io::database_t() ) );
                _early = _manager->register_funk( [this]( motor::string_in_t name, msl_ptr_t object )
                {
                    ++_early_calls;
                    check( "[callback windows] early callback refers to the shared ready msl",
                        object == _shader && name == "callbacks.shader" && object->is_ready() );
                } );
                _future = _created->get_future();
                _manager->add( "callbacks.shader", motor::string_t( R"(
                    config callback_windows
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
                            void main() { out.color = vec4_t( 0.15, 0.8, 0.65, 1.0 ) ; }
                        }
                    }
                )" ), [created = _created]( manager_t::completion_funk_data const & data )
                {
                    data.msl->link_geometry( "callbacks.triangle" );
                    data.msl->add_variable_set( motor::shared( motor::graphics::variable_set_t() ) );
                    created->set_value( data.msl );
                    return false;
                } );
            }
            open( 0 );
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            if( _quitting ) return;
            if( _shader == nullptr && _future.valid() &&
                _future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
                _shader = _future.get();
            if( _shader != nullptr ) _manager->on_update();
        }

        void_t on_first_frame( window_id_t const id, frontend_t * fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto const index = slot( id );
            if( index == size_t( -1 ) ) return;
            fe->configure< motor::graphics::state_object_t >( &_state );
            fe->configure< motor::graphics::geometry_object_t >( &_geometry );
            if( index != 0 )
            {
                check( "[callback windows] new window has no ready shader before manager init",
                    _shader != nullptr && fe->decode( _shader ).first != motor::graphics::object_state::ready );
            }
            // This is essential once on_frame_done()/on_update() have drained the initial work lists.
            _manager->on_render_init( fe );
            _windows[index].initialized = true;
        }

        void_t on_render( window_id_t const id, frontend_t * fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto const index = slot( id );
            if( _quitting || index == size_t( -1 ) ) return;
            _manager->on_render( fe );
            fe->push( &_state );
            if( _shader != nullptr )
            {
                auto const status = fe->decode( _shader );
                if( status.first == motor::graphics::object_state::ready &&
                    status.second == motor::graphics::result::ok )
                {
                    if( _windows[index].frames == 0 )
                        motor::log::global_t::status( "[callback windows] shader ready in window slot " + motor::to_string( index ) );
                    motor::graphics::gen4::backend_t::render_detail_t detail;
                    detail.varset = 0;
                    fe->render( _shader, detail );
                    ++_windows[index].frames;
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }

        void_t on_last_frame( window_id_t const id, frontend_t * fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto const index = slot( id );
            if( index == size_t( -1 ) ) return;
            _manager->on_render_release( fe );
            fe->release< motor::graphics::geometry_object_t >( &_geometry );
            fe->release< motor::graphics::state_object_t >( &_state );
            _windows[index].last_frame = true;
            fe->fence( [state = _completion, index]( frontend_t::fence_phase const phase )
            {
                if( phase == frontend_t::fence_phase::execute ) state->released[index].store( true );
            } );
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( _manager == nullptr ) return;
            _manager->on_frame_done();
            if( _quitting ) return;
            switch( _step )
            {
            case 0:
                if( _windows[0].frames < 20 || _early_calls != 1 ) break;
                check( "[callback windows] first window renders before second opens", true );
                open( 1 );
                ++_step;
                break;
            case 1:
                if( _windows[1].frames < 20 ) break;
                check( "[callback windows] second window configured the existing msl", _windows[1].initialized );
                _late = _manager->register_funk( [this]( motor::string_in_t, msl_ptr_t object )
                {
                    ++_late_calls;
                    check( "[callback windows] late callback receives the same msl", object == _shader );
                } );
                ++_step;
                break;
            case 2:
                if( _late_calls != 1 ) break;
                _late = _manager->unregister_funk( _late );
                close_secondary( 1 );
                ++_step;
                break;
            case 3:
                if( !_completion->released[1].load() || _windows[0].frames < _main_at_close + 20 ) break;
                check( "[callback windows] first window continues after second closes", _windows[1].last_frame );
                open( 2 );
                ++_step;
                break;
            case 4:
                if( _windows[2].frames < 20 ) break;
                check( "[callback windows] reopened window configured the same msl again", _windows[2].initialized );
                close_secondary( 2 );
                ++_step;
                break;
            case 5:
                if( !_completion->released[2].load() || _windows[0].frames < _main_at_close + 20 ) break;
                check( "[callback windows] both secondary lifecycles finished", _windows[1].last_frame && _windows[2].last_frame );
                check( "[callback windows] no duplicate callbacks after window reopen", _early_calls == 1 && _late_calls == 1 );
                *_passed = _ok;
                _quitting = true;
                close();
                break;
            }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( !_quitting && std::chrono::steady_clock::now() - _started > std::chrono::seconds( 30 ) )
            {
                check( "[callback windows] lifecycle completed before timeout", false );
                motor::log::global_t::status( "[callback windows] stalled at step " + motor::to_string( _step ) );
                _quitting = true;
                close();
            }
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        {
            if( state.close_changed && id == _windows[0].id && !_quitting )
            {
                _quitting = true;
                close();
            }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            _manager->unregister_funk( _early );
            _manager->unregister_funk( _late );
            _shader = nullptr;
            _manager.reset();
        }
    };
}

int run_msl_manager_windows( bool const d3d )
{
    bool passed = false;
    motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier(
        motor::shared( callback_windows::app( d3d, &passed ) ) );
    auto const result = carrier->exec();
    motor::release( motor::move( carrier ) );
    motor::concurrent::global_t::deinit();
    motor::io::global_t::deinit();
    motor::profiling::global_t::deinit();
    motor::log::global_t::deinit();
    bool const clean = motor::memory::global_t::dump_to_std() == 0;
    return result != 0 ? result : ( passed && clean ? 0 : 1 );
}
