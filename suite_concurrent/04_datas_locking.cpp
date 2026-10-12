#include <motor/concurrent/typedefs.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>
#include <motor/platform/graphics/datas.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <latch>
#include <mutex>

namespace this_file
{
    using namespace motor::core::types ;

    // Standard-library synchronization keeps the test harness independent of datas.
    class watchdog
    {
        std::mutex _mutex ;
        std::condition_variable _cv ;
        bool_t _done = false ;
        motor::concurrent::thread_t _thread ;

    public:
        watchdog( void_t ) : _thread( [this]( void_t )
        {
            std::unique_lock<std::mutex> lock( _mutex ) ;
            if( _cv.wait_for( lock, std::chrono::seconds( 20 ), [this]( void_t )
                { return _done ; } ) ) return ;
            motor::log::global_t::test( "[datas] watchdog: no completion within 20s; possible deadlock", false ) ;
            // Joining a deadlocked worker would hang too. CTest also has an outer timeout.
            std::_Exit( EXIT_FAILURE ) ;
        } ) {}

        watchdog( watchdog const & ) = delete ;
        watchdog & operator=( watchdog const & ) = delete ;

        ~watchdog( void_t )
        {
            {
                std::lock_guard<std::mutex> lock( _mutex ) ;
                _done = true ;
            }
            _cv.notify_one() ;
            _thread.join() ;
        }
    } ;

    struct dummy_data
    {
        inline static std::atomic_size_t alive { 0 } ;
        inline static std::atomic_size_t invalidations { 0 } ;

        // Atomics let a broken exclusion check report overlapping callbacks without
        // introducing additional data races in the test payload itself.
        std::atomic_size_t value { 0 }, mirror { 0 }, inside { 0 } ;

        dummy_data( void_t ) { alive.fetch_add( 1 ) ; }
        ~dummy_data( void_t ) { alive.fetch_sub( 1 ) ; }

        void_t invalidate( motor::string_in_t ) noexcept
        {
            invalidations.fetch_add( 1 ) ;
            value.store( 0 ) ;
            mirror.store( 0 ) ;
        }
    } ;

    using datas_t = motor::platform::datas<dummy_data> ;

    size_t create( datas_t & data, motor::string_in_t name )
    {
        size_t id = size_t( -1 ) ;
        data.access( id, name, []( dummy_data & item )
        {
            item.value.store( 0 ) ;
            item.mirror.store( 0 ) ;
            return true ;
        } ) ;
        return id ;
    }

    bool_t lifecycle( void_t )
    {
        auto const invalidations_before = dummy_data::invalidations.load() ;
        datas_t data ;
        auto const first = create( data, "first" ) ;
        dummy_data * original = nullptr ;
        bool_t ok = data.access( first, [&]( dummy_data & item )
        {
            original = &item ;
            item.value.store( 42 ) ;
        } ) ;

        // Force wrapper-vector growth, then verify existing data and lock state.
        for( size_t i = 1; i < 81; ++i )
            ok = (create( data, "entry_" + motor::to_string( i ) ) != size_t( -1 )) && ok ;

        size_t visits = 0 ;
        data.for_each( [&]( dummy_data & ) { ++visits ; } ) ;
        ok = (visits == 81 && data.find_by_name( "first" ) == first) && ok ;
        ok = data.access_by_name( "first", [&]( size_t const id, dummy_data & item )
        {
            ok = (id == first && &item == original && item.value.load() == 42) && ok ;
        } ) && ok ;

        bool_t called = false ;
        ok = !data.access( size_t( -1 ), [&]( dummy_data & ) { called = true ; } ) && ok ;
        ok = !data.access( size_t( 9999 ), [&]( dummy_data & ) { called = true ; } ) && ok ;
        ok = !data.access_by_name( "missing", [&]( size_t const, dummy_data & ) { called = true ; } ) && ok ;
        ok = !called && ok ;

        ok = data.invalidate( first ) && ok ;
        ok = (data.find_by_name( "first" ) == size_t( -1 )) && ok ;
        auto const reused = create( data, "reused" ) ;
        ok = (reused == first) && ok ;
        ok = data.access( reused, [&]( dummy_data & item ) { ok = (item.value.load() == 0) && ok ; } ) && ok ;
        data.invalidate_and_clear() ;
        return ok && data.size() == 0 && dummy_data::alive.load() == 0 &&
            dummy_data::invalidations.load() - invalidations_before == 82 ;
    }

    bool_t recursive( void_t )
    {
        datas_t data ;
        auto id = create( data, "recursive" ) ;
        bool_t ok = true ;
        size_t visits = 0 ;
        data.access( id, [&]( dummy_data & outer )
        {
            ++visits ;
            // Match the nested access-by-name path used by geometry-link updates.
            ok = data.access( id, "recursive", [&]( dummy_data & inner )
            {
                ++visits ;
                ok = (&inner == &outer) && ok ;
                auto const result = data.try_access( id, [&]( dummy_data & nested )
                {
                    ++visits ;
                    ok = (&nested == &outer) && ok ;
                } ) ;
                ok = result.second && ok ;
                return true ;
            } ) && ok ;

            // Destroying the inner guards must not release the outer ownership.
            std::atomic_bool intruded { false } ;
            motor::concurrent::thread_t contender( [&]( void_t )
            {
                auto const result = data.try_access( id, [&]( dummy_data & ) { intruded.store( true ) ; } ) ;
                if( result.second ) intruded.store( true ) ;
            } ) ;
            contender.join() ;
            ok = !intruded.load() && ok ;
        } ) ;

        bool_t acquired = false ;
        motor::concurrent::thread_t successor( [&]( void_t )
        {
            auto const result = data.try_access( id, [&]( dummy_data & ) { acquired = true ; } ) ;
            acquired = acquired && result.second ;
        } ) ;
        successor.join() ;
        return ok && visits == 3 && acquired ;
    }

    bool_t try_access( void_t )
    {
        datas_t data ;
        auto const id = create( data, "try" ) ;
        bool_t ok = true ;
        bool_t called = false ;
        auto const missing = data.try_access( size_t( -1 ), [&]( dummy_data & ) { called = true ; } ) ;
        ok = !missing.first && !missing.second && !called ;

        data.access( id, [&]( dummy_data & )
        {
            motor::concurrent::thread_t contender( [&]( void_t )
            {
                for( size_t i = 0; i < 100; ++i )
                {
                    auto const result = data.try_access( id, [&]( dummy_data & ) { called = true ; } ) ;
                    ok = result.first && !result.second && !called && ok ;
                }
            } ) ;
            contender.join() ;
        } ) ;

        motor::concurrent::thread_t successor( [&]( void_t )
        {
            auto const result = data.try_access( id, [&]( dummy_data & ) { called = true ; } ) ;
            ok = result.second && called && ok ;
        } ) ;
        successor.join() ;
        return ok ;
    }

    bool_t independent( void_t )
    {
        datas_t data ;
        auto const first = create( data, "first" ) ;
        auto const second = create( data, "second" ) ;
        bool_t entered = false ;
        data.access( first, [&]( dummy_data & )
        {
            motor::concurrent::thread_t worker( [&]( void_t )
            {
                data.access( second, [&]( dummy_data & item )
                {
                    item.value.store( 7 ) ;
                    entered = true ;
                } ) ;
            } ) ;
            worker.join() ;
        } ) ;
        return entered ;
    }

    bool_t contention( void_t )
    {
        datas_t data ;
        auto const id = create( data, "shared" ) ;
        constexpr size_t worker_count = 6 ;
        constexpr size_t rounds = 2000 ;
        std::latch ready( worker_count ), start( 1 ) ;
        std::atomic_bool ok { true } ;
        motor::vector<motor::concurrent::thread_t> workers ;
        for( size_t i = 0; i < worker_count; ++i )
        {
            workers.emplace_back( [&]( void_t )
            {
                ready.count_down() ;
                start.wait() ;
                for( size_t n = 0; n < rounds; ++n )
                {
                    if( !data.access( id, [&]( dummy_data & item )
                    {
                        if( item.inside.fetch_add( 1 ) != 0 ) ok.store( false ) ;
                        auto const next = item.value.load() + 1 ;
                        if( item.mirror.load() != (next - 1) * 3 ) ok.store( false ) ;
                        item.value.store( next ) ;
                        if( n % 32 == 0 ) std::this_thread::yield() ;
                        item.mirror.store( next * 3 ) ;
                        if( item.inside.fetch_sub( 1 ) != 1 ) ok.store( false ) ;
                    } ) ) ok.store( false ) ;
                }
            } ) ;
        }
        ready.wait() ;
        start.count_down() ;
        for( auto & worker : workers ) worker.join() ;
        data.access( id, [&]( dummy_data & item )
        {
            if( item.value.load() != worker_count * rounds ||
                item.mirror.load() != worker_count * rounds * 3 || item.inside.load() != 0 ) ok.store( false ) ;
        } ) ;
        return ok.load() ;
    }

    bool_t wakeup( void_t )
    {
        datas_t data ;
        auto const id = create( data, "handoff" ) ;
        bool_t ok = true ;
        for( size_t round = 0; round < 32; ++round )
        {
            std::latch attempted( 4 ) ;
            motor::vector<motor::concurrent::thread_t> waiters ;
            std::atomic_size_t completed { 0 } ;
            std::atomic_bool worker_ok { true } ;
            data.access( id, [&]( dummy_data & item )
            {
                item.value.store( round + 1 ) ;
                for( size_t i = 0; i < 4; ++i )
                {
                    waiters.emplace_back( [&]( void_t )
                    {
                        auto const tried = data.try_access( id, [&]( dummy_data & ) { worker_ok.store( false ) ; } ) ;
                        if( tried.second ) worker_ok.store( false ) ;
                        attempted.count_down() ;
                        if( !data.access( id, [&]( dummy_data & acquired )
                        {
                            if( acquired.value.load() != round + 1 ) worker_ok.store( false ) ;
                            completed.fetch_add( 1 ) ;
                        } ) ) worker_ok.store( false ) ;
                    } ) ;
                }
                attempted.wait() ;
                // Give waiters an opportunity to sleep; correctness does not depend
                // on whether they reach cv.wait before or after the release.
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) ) ;
                ok = (completed.load() == 0) && ok ;
            } ) ;
            for( auto & waiter : waiters ) waiter.join() ;
            ok = worker_ok.load() && completed.load() == 4 && ok ;
        }
        return ok ;
    }
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    int result = 2 ;
    {
        motor::string_t const mode = argc == 2 ? argv[1] : "--smoke" ;
        struct scenario { char const * name ; bool_t (*run)( void_t ) ; } ;
        scenario const cases[] = {
            { "lifecycle", this_file::lifecycle }, { "recursive", this_file::recursive },
            { "try_access", this_file::try_access }, { "independent", this_file::independent },
            { "contention", this_file::contention }, { "wakeup", this_file::wakeup }
        } ;
        for( auto const & test : cases )
        {
            if( mode != "--smoke" && mode != test.name ) continue ;
            motor::log::global_t::status( "[datas] starting " + motor::string_t( test.name ) ) ;
            bool_t ok = false ;
            {
                this_file::watchdog timeout ;
                ok = test.run() ;
            }
            ok = this_file::dummy_data::alive.load() == 0 && ok ;
            motor::log::global_t::test( "[datas] " + motor::string_t( test.name ), ok ) ;
            if( result == 2 ) result = 0 ;
            if( !ok ) result = 1 ;
        }
        if( result == 2 ) motor::log::global_t::error(
            "Usage: 04_datas_locking [--smoke|lifecycle|recursive|try_access|independent|contention|wakeup]" ) ;
    }
    motor::log::global_t::deinit() ;
    bool_t const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
