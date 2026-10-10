#include <motor/concurrent/mrsw.hpp>
#include <motor/concurrent/sync_object.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <atomic>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;

    bool_t semaphore( void_t )
    {
        motor::concurrent::semaphore_t pending( 4 ) ;
        motor::vector<size_t> values( 4, 0 ) ;
        motor::vector<motor::concurrent::thread_t> workers ;
        for( size_t i = 0; i < values.size(); ++i )
        {
            workers.emplace_back( [&, i]( void_t )
            {
                values[i] = (i + 1) * 11 ;
                pending.decrement() ;
            } ) ;
        }
        pending.wait() ;
        bool_t ok = pending == 0 ;
        for( size_t i = 0; i < values.size(); ++i ) ok = (values[i] == (i + 1) * 11) && ok ;
        for( auto & worker : workers ) worker.join() ;
        ok = !pending.decrement() && ok ;
        pending.wait( 0, 1 ) ;
        ok = (pending == 1) && ok ;
        pending.decrement() ;
        return ok ;
    }

    bool_t signal( void_t )
    {
        motor::concurrent::sync_object_t ready ;
        ready.wait( false ) ;
        ready.set_and_signal() ;
        ready.wait() ;
        bool_t ok = ready.is_signaled() ;
        for( size_t round = 0; round < 8; ++round )
        {
            ready.reset() ;
            ok = ready.is_not_signaled() && ok ;
            std::atomic_size_t entered { 0 }, finished { 0 } ;
            size_t payload = 0 ;
            motor::vector<motor::concurrent::thread_t> workers ;
            for( size_t i = 0; i < 4; ++i )
            {
                workers.emplace_back( [&]( void_t )
                {
                    entered.fetch_add( 1 ) ;
                    ready.wait() ;
                    if( payload == 42 ) finished.fetch_add( 1 ) ;
                } ) ;
            }
            while( entered.load() != 4 ) std::this_thread::yield() ;
            ok = (finished.load() == 0) && ok ;
            payload = 42 ;
            ready.set_and_signal() ;
            for( auto & worker : workers ) worker.join() ;
            ok = (finished.load() == 4) && ok ;
        }
        return ok ;
    }

    bool_t readers( void_t )
    {
        motor::concurrent::mrsw_t lock ;
        motor::concurrent::sync_object_t leave ;
        std::atomic_size_t inside { 0 } ;
        motor::vector<motor::concurrent::thread_t> workers ;
        for( size_t i = 0; i < 4; ++i )
        {
            workers.emplace_back( [&]( void_t )
            {
                motor::concurrent::mrsw_t::reader_lock_t guard( lock ) ;
                inside.fetch_add( 1 ) ;
                leave.wait() ;
            } ) ;
        }
        // All readers must enter together; an accidentally exclusive lock times out in CTest.
        while( inside.load() != 4 ) std::this_thread::yield() ;
        leave.set_and_signal() ;
        for( auto & worker : workers ) worker.join() ;
        motor::concurrent::mrsw_t::writer_lock_t guard( lock ) ;
        return inside.load() == 4 ;
    }

    bool_t contention( void_t )
    {
        motor::concurrent::mrsw_t lock ;
        motor::concurrent::sync_object_t start ;
        std::atomic_size_t writers { 0 }, readers { 0 }, value { 0 }, mirror { 0 } ;
        std::atomic_bool ok { true } ;
        motor::vector<motor::concurrent::thread_t> workers ;
        for( size_t i = 0; i < 6; ++i )
        {
            workers.emplace_back( [&, i]( void_t )
            {
                start.wait() ;
                for( size_t n = 0; n < 500; ++n )
                {
                    if( i < 2 )
                    {
                        motor::concurrent::mrsw_t::writer_lock_t guard( lock ) ;
                        if( writers.fetch_add( 1 ) != 0 || readers.load() != 0 ) ok.store( false ) ;
                        auto const next = value.load() + 1 ;
                        value.store( next ) ;
                        std::this_thread::yield() ;
                        mirror.store( next * 3 ) ;
                        writers.fetch_sub( 1 ) ;
                    }
                    else
                    {
                        motor::concurrent::mrsw_t::reader_lock_t guard( lock ) ;
                        readers.fetch_add( 1 ) ;
                        if( writers.load() != 0 || mirror.load() != value.load() * 3 ) ok.store( false ) ;
                        readers.fetch_sub( 1 ) ;
                    }
                }
            } ) ;
        }
        start.set_and_signal() ;
        for( auto & worker : workers ) worker.join() ;
        return ok.load() && value.load() == 1000 && mirror.load() == 3000 ;
    }
}

int main( int argc, char ** argv )
{
    int result = 2 ;
    {
        motor::string_t const mode = argc == 2 ? argv[1] : "" ;
        bool ok = false ;
        if( mode == "semaphore" ) ok = this_file::semaphore() ;
        else if( mode == "signal" ) ok = this_file::signal() ;
        else if( mode == "readers" ) ok = this_file::readers() ;
        else if( mode == "contention" ) ok = this_file::contention() ;
        else
        {
            motor::log::global_t::error( "Usage: 01_sync_primitives semaphore|signal|readers|contention" ) ;
            motor::log::global_t::deinit() ;
            return result ;
        }
        result = ok ? 0 : 1 ;
        if( ok ) motor::log::global_t::status<256>( "[sync] %s: PASS", mode.c_str() ) ;
        else motor::log::global_t::error<256>( "[sync] %s: FAIL", mode.c_str() ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
