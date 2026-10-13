#include <motor/concurrent/typedefs.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>
#include <motor/platform/graphics/datas.hpp>
#include "support/measured_datas.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <latch>
#include <mutex>

namespace this_file
{
    using namespace motor::core::types ;
    namespace diag = motor::tests::diagnostic ;
    using clock = std::chrono::steady_clock ;

    class watchdog
    {
        std::mutex _mtx ;
        std::condition_variable _cv ;
        bool_t _done = false ;
        motor::concurrent::thread_t _thread ;
    public:
        watchdog( void_t ) : _thread( [this]( void_t )
        {
            std::unique_lock<std::mutex> lock( _mtx ) ;
            if( _cv.wait_for( lock, std::chrono::seconds( 30 ), [this]( void_t ) { return _done ; } ) ) return ;
            motor::log::global_t::test( "[datas-cost] watchdog", false ) ;
            std::_Exit( EXIT_FAILURE ) ;
        } ) {}
        ~watchdog( void_t )
        {
            { std::lock_guard<std::mutex> lock( _mtx ) ; _done = true ; }
            _cv.notify_one() ; _thread.join() ;
        }
    } ;

    struct payload
    {
        size_t value = 0 ;
        void_t invalidate( motor::string_in_t ) noexcept { value = 0 ; }
    } ;

    template< typename Data >
    size_t create( Data & data, motor::string_in_t name )
    {
        size_t id = size_t( -1 ) ;
        data.access( id, name, []( payload & p ) { p.value = 0 ; return true ; } ) ;
        return id ;
    }

    void_t reset( diag::mode const mode )
    {
        diag::local = {} ;
        diag::local.recording = mode ;
    }

    void_t add( diag::counters & total, diag::counters const & from )
    {
        for( size_t i = 0 ; i < 4 ; ++i )
        {
            total.regions[i].calls += from.regions[i].calls ;
            total.regions[i].nanoseconds += from.regions[i].nanoseconds ;
        }
        total.recursive += from.recursive ; total.failed_try += from.failed_try ;
    }

    void_t report( motor::string_in_t label, double_t const ms, diag::counters const & c )
    {
        motor::log::global_t::status( "[datas-cost] " + label + " wall_ms=" + motor::to_string( ms ) ) ;
        char_cptr_t const names[] = { "container_read_acquire", "container_write_acquire", "entry_acquire", "callback_inclusive" } ;
        for( size_t i = 0 ; i < 4 ; ++i )
        {
            auto const & s = c.regions[i] ;
            motor::log::global_t::status( motor::string_t( "  " ) + names[i] + " calls=" + motor::to_string( s.calls ) +
                " total_us=" + motor::to_string( double_t( s.nanoseconds ) / 1000.0 ) ) ;
        }
        motor::log::global_t::status( "  recursive=" + motor::to_string( c.recursive ) +
            " failed_try=" + motor::to_string( c.failed_try ) ) ;
    }

    template< bool Measure >
    bool_t run( size_t const rounds, int_t const scenario, diag::mode const mode, char_cptr_t label )
    {
        using data_t = std::conditional_t<Measure, motor::tests::datas<payload, true>,
            motor::platform::datas<payload>> ;
        data_t data ;
        size_t const ids[] = { create( data, "render" ), create( data, "support" ) } ;
        // Warm the same access path, outside the measured interval.
        for( size_t i = 0 ; i < 500 ; ++i ) data.access( ids[0], []( payload & p ) { p.value = 0 ; } ) ;
        diag::counters metrics[2] ;
        std::atomic_bool ok { true } ;
        std::latch ready( 2 ), start( 1 ) ;
        auto worker = [&]( size_t const index )
        {
            reset( mode ) ;
            ready.count_down() ; start.wait() ;
            size_t const id = scenario == 2 ? ids[index] : ids[0] ;
            if( index == 0 || scenario >= 2 )
            {
                for( size_t i = 0 ; i < rounds ; ++i )
                {
                    if( scenario == 4 && index == 1 )
                    {
                        // A bounded structural-writer workload, not a shader-compiler simulation.
                        if( i < 64 && create( data, "new_" + motor::to_string( i ) ) == size_t( -1 ) ) ok.store( false ) ;
                        continue ;
                    }
                    if( !data.access( id, [&]( payload & p )
                    {
                        ++p.value ;
                        if( scenario == 1 )
                            if( !data.access( id, []( payload & inner ) { ++inner.value ; } ) ) ok.store( false ) ;
                    } ) ) ok.store( false ) ;
                }
            }
            metrics[index] = diag::local ;
            reset( diag::mode::off ) ;
        } ;
        motor::concurrent::thread_t a( [&worker]( void_t ) { worker( 0 ) ; } ) ;
        motor::concurrent::thread_t b( [&worker]( void_t ) { worker( 1 ) ; } ) ;
        ready.wait() ;
        auto const begin = clock::now() ;
        start.count_down() ; a.join() ; b.join() ;
        double_t const ms = std::chrono::duration<double_t, std::milli>( clock::now() - begin ).count() ;
        diag::counters total ; add( total, metrics[0] ) ; add( total, metrics[1] ) ;
        size_t values[2] = {} ;
        for( size_t i = 0 ; i < 2 ; ++i ) data.access( ids[i], [&]( payload & p ) { values[i] = p.value ; } ) ;
        bool_t const correct = scenario == 2 ? values[0] == rounds && values[1] == rounds :
            values[0] == rounds * (scenario == 1 || scenario == 3 ? 2 : 1) && values[1] == 0 ;
        if constexpr( Measure )
        {
            if( mode != diag::mode::off && scenario <= 3 )
            {
                auto const expected = rounds * (scenario == 0 ? 1 : 2) ;
                ok.store( ok.load() && total.regions[0].calls == expected && total.regions[2].calls == expected &&
                    total.regions[3].calls == expected && total.recursive == (scenario == 1 ? rounds : 0) ) ;
            }
        }
        report( label, ms, total ) ;
        return correct && ok.load() ;
    }

    bool_t failed_try( void_t )
    {
        motor::tests::datas<payload, true> data ;
        auto const id = create( data, "held" ) ;
        diag::counters measured ;
        bool_t ok = true ;
        data.access( id, [&]( payload & )
        {
            motor::concurrent::thread_t contender( [&]( void_t )
            {
                reset( diag::mode::timings ) ;
                for( size_t i = 0 ; i < 100 ; ++i )
                {
                    auto const result = data.try_access( id, [&]( payload & ) { ok = false ; } ) ;
                    ok = ok && result.first && !result.second ;
                }
                measured = diag::local ; reset( diag::mode::off ) ;
            } ) ;
            contender.join() ;
        } ) ;
        report( "held entry / failed try", 0.0, measured ) ;
        return ok && measured.failed_try == 100 && measured.regions[2].calls == 100 && measured.regions[3].calls == 0 ;
    }
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    size_t rounds = 2000 ;
    if( argc > 2 ) return 2 ;
    if( argc == 2 )
    {
        motor::string_t const arg( argv[1] ) ;
        if( arg == "--benchmark" ) rounds = 100000 ;
        else if( arg != "--smoke" ) return 2 ;
    }
    bool_t ok = true ;
    {
        this_file::watchdog timeout ;
        namespace diag = motor::tests::diagnostic ;
        char_cptr_t const names[] = { "serial", "recursive", "independent entries", "same entry", "container growth" } ;
        for( int_t s = 0 ; s < 5 ; ++s )
        {
            motor::log::global_t::status( motor::string_t( "[datas-cost] scenario: " ) + names[s] ) ;
            ok = this_file::run<false>( rounds, s, diag::mode::off, "original engine datas" ) && ok ;
            ok = this_file::run<true>( rounds, s, diag::mode::off, "recording off" ) && ok ;
            ok = this_file::run<true>( rounds, s, diag::mode::counters, "counters only" ) && ok ;
            ok = this_file::run<true>( rounds, s, diag::mode::timings, "timed" ) && ok ;
        }
        ok = this_file::failed_try() && ok ;
    }
    motor::log::global_t::test( "[datas-cost] counts, payloads and exclusion (no performance threshold)", ok ) ;
    motor::log::global_t::deinit() ;
    return motor::memory::global_t::dump_to_std() == 0 && ok ? 0 : 1 ;
}
