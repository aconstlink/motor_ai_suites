#include <motor/gfx/manager/msl_manager.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/vector>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

using motor::core::types::void_t;

namespace this_file
{
    using manager_t = motor::gfx::msl_manager_t;
    using msl_ptr_t = motor::graphics::msl_object_mtr_t;

    struct event
    {
        size_t listener;
        motor::string_t name;
        msl_ptr_t object;
    };

    struct checks
    {
        bool ok = true;

        void check( char const * name, bool const passed )
        {
            motor::log::global_t::test( name, passed );
            ok = passed && ok;
        }
    };

    // The manager owns these pointers. No graphics backend or shader compiler runs here.
    msl_ptr_t add( manager_t & manager, motor::string_cref_t name, checks & result )
    {
        auto ready = std::make_shared< std::promise< msl_ptr_t > >();
        auto future = ready->get_future();
        manager.add( name, motor::string_t( "// callback test: source is not compiled" ),
            [ready]( manager_t::completion_funk_data const & data )
            {
                ready->set_value( data.msl );
                return false;
            } );

        bool const completed = future.wait_for( std::chrono::seconds( 5 ) ) ==
            std::future_status::ready;
        result.check( "[callbacks] asynchronous msl creation completed", completed );
        if( !completed ) return nullptr;
        auto const object = future.get();
        result.check( "[callbacks] created object has the requested name",
            object != nullptr && object->name() == name );
        return object;
    }

    void ready( msl_ptr_t object, size_t const backend = 0 )
    {
        motor::graphics::object::data_manipulator status( object, backend );
        status.change_to_in_transit();
        status.change_to_ready( motor::graphics::result::ok );
    }

    bool run( void_t )
    {
        checks result;
        motor::vector< event > events;
        auto const update_thread = std::this_thread::get_id();
        bool valid_delivery = true;

        // Direct-source add() needs no filesystem database initialization.
        manager_t manager( motor::shared( motor::io::database_t() ) );
        auto subscribe = [&]( size_t const listener )
        {
            return manager.register_funk(
                [&, listener]( motor::string_in_t name, msl_ptr_t object )
                {
                    valid_delivery = valid_delivery && object != nullptr &&
                        object->name() == name && object->is_ready() &&
                        std::this_thread::get_id() == update_thread;
                    events.emplace_back( event{ listener, name, object } );
                } );
        };
        auto count = [&]( size_t const listener, msl_ptr_t object )
        {
            size_t value = 0;
            for( auto const & entry : events )
                if( entry.listener == listener && entry.object == object ) ++value;
            return value;
        };

        auto const early = subscribe( 0 );
        manager.on_update();
        result.check( "[callbacks] empty manager does not notify", events.empty() );

        auto const a = add( manager, "callbacks.a", result );
        auto const b = add( manager, "callbacks.b", result );
        if( a == nullptr || b == nullptr ) return false;
        manager.on_update();
        manager.on_update();
        result.check( "[callbacks] pending msls do not notify", events.empty() );

        ready( a );
        manager.on_update();
        manager.on_update();
        result.check( "[callbacks] early listener receives newly ready msl exactly once",
            count( 0, a ) == 1 && count( 0, b ) == 0 && events.size() == 1 );

        auto const late = subscribe( 1 );
        ready( b );
        manager.on_update();
        manager.on_update();
        result.check( "[callbacks] late listener receives existing and fresh msls once",
            count( 1, a ) == 1 && count( 1, b ) == 1 &&
            count( 0, a ) == 1 && count( 0, b ) == 1 && events.size() == 4 );

        auto const removed_before_update = subscribe( 2 );
        result.check( "[callbacks] unregister returns invalid registration id",
            manager.unregister_funk( removed_before_update ) == size_t( -1 ) );
        manager.on_update();
        result.check( "[callbacks] unregister suppresses undelivered backlog", events.size() == 4 );

        auto const c = add( manager, "callbacks.c", result );
        if( c == nullptr ) return false;
        auto const pending_listener = subscribe( 3 );
        manager.on_update();
        manager.on_update();
        result.check( "[callbacks] registration while pending catches up on ready msls only",
            count( 3, a ) == 1 && count( 3, b ) == 1 && count( 3, c ) == 0 && events.size() == 6 );

        manager.unregister_funk( early );
        ready( c );
        motor::graphics::object::data_manipulator( c, 1 ).change_to_in_transit();
        manager.on_update();
        result.check( "[callbacks] waits until every backend is ready", events.size() == 6 );
        ready( c, 1 );
        manager.on_update();
        manager.on_update();
        result.check( "[callbacks] remaining listeners receive pending completion; removed ones do not",
            count( 0, c ) == 0 && count( 2, c ) == 0 && count( 1, c ) == 1 &&
            count( 3, c ) == 1 && events.size() == 8 );

        manager.unregister_funk( late );
        manager.unregister_funk( pending_listener );
        auto const d = add( manager, "callbacks.d", result );
        if( d == nullptr ) return false;
        ready( d );
        manager.on_update();
        result.check( "[callbacks] completion with no listeners is safe", events.size() == 8 );

        // Cross the initial five-slot capacity, then remove alternating registrations.
        motor::vector< size_t > registrations;
        for( size_t i = 0; i < 8; ++i ) registrations.emplace_back( subscribe( 10 + i ) );
        manager.on_update();
        manager.on_update();
        bool backlog = events.size() == 40;
        for( size_t i = 0; i < 8; ++i )
            for( auto object : { a, b, c, d } ) backlog = count( 10 + i, object ) == 1 && backlog;
        result.check( "[callbacks] growing listener list receives complete backlog exactly once", backlog );

        for( size_t i = 0; i < registrations.size(); i += 2 )
            registrations[i] = manager.unregister_funk( registrations[i] );
        auto const replacement = subscribe( 20 );
        auto const e = add( manager, "callbacks.e", result );
        if( e == nullptr ) return false;
        ready( e );
        manager.on_update();
        manager.on_update();
        bool reused = events.size() == 49;
        for( size_t i = 0; i < 8; ++i )
            reused = count( 10 + i, e ) == ( i % 2 ) && reused;
        for( auto object : { a, b, c, d, e } ) reused = count( 20, object ) == 1 && reused;
        result.check( "[callbacks] holes and reused registration slots preserve correct delivery", reused );

        for( auto id : registrations ) manager.unregister_funk( id );
        manager.unregister_funk( replacement );
        result.check( "[callbacks] invalid unregister is harmless",
            manager.unregister_funk( size_t( -1 ) ) == size_t( -1 ) );
        manager.on_update();
        result.check( "[callbacks] no extra callbacks after removing all listeners", events.size() == 49 );
        result.check( "[callbacks] names, pointers, readiness and update-thread delivery", valid_delivery );
        return result.ok;
    }
}

int run_msl_manager_windows( bool const d3d );

int main( int argc, char ** argv )
{
    if( argc == 2 )
    {
        motor::string_t const mode( argv[1] );
        if( mode == "--windows-gl" ) return run_msl_manager_windows( false );
#ifdef _WIN32
        if( mode == "--windows-d3d" ) return run_msl_manager_windows( true );
#endif
    }
    if( argc != 1 )
    {
        motor::log::global_t::status( "Usage: 22_msl_manager_callbacks [--windows-gl|--windows-d3d]" );
        motor::log::global_t::deinit();
        return 2;
    }
    bool const ok = this_file::run();
    motor::log::global_t::deinit();
    bool const clean = motor::memory::global_t::dump_to_std() == 0;
    return ok && clean ? 0 : 1;
}
