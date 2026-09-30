#include <motor/concurrent/global.h>
#include <motor/concurrent/parallel_for.hpp>
#include <motor/concurrent/thread_pool.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <atomic>
#include <cstdint>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;
    using range_t = motor::concurrent::range_1d<size_t> ;

    // Atomic counters also make duplicate visits observable without a test-side race.
    class coverage
    {
        motor::vector<std::atomic_uint> _visits ;
        std::atomic_bool _outside { false } ;
        std::atomic_uint64_t _sum { 0 } ;
        size_t _begin ;
        size_t _end ;

    public:
        coverage( size_t const begin, size_t const end )
            : _visits( end ), _begin( begin ), _end( end )
        {
            for( auto & v : _visits ) v.store( 0 ) ;
        }

        void_t visit( size_t const i )
        {
            if( i < _begin || i >= _end )
            {
                _outside.store( true ) ;
                return ;
            }
            _visits[i].fetch_add( 1 ) ;
            _sum.fetch_add( uint64_t( i ) * i + 3 ) ;
        }

        void_t visit( range_t const & r )
        {
            if( r.begin() < _begin || r.end() > _end || r.end() < r.begin() )
            {
                _outside.store( true ) ;
                return ;
            }
            for( size_t i = r.begin(); i < r.end(); ++i ) visit( i ) ;
        }

        bool_t check( void_t ) const
        {
            uint64_t expected = 0 ;
            bool_t ok = !_outside.load() ;
            for( size_t i = 0; i < _end; ++i )
            {
                ok = ( _visits[i].load() == ( i < _begin ? 0u : 1u ) ) && ok ;
                if( i >= _begin ) expected += uint64_t( i ) * i + 3 ;
            }
            return ok && _sum.load() == expected ;
        }
    } ;

    bool_t threads( void_t )
    {
        coverage data( 0, 4096 ) ;
        {
            motor::vector<motor::concurrent::thread_t> workers ;
            for( size_t t = 0; t < 4; ++t )
            {
                workers.emplace_back( [&, t]( void_t )
                {
                    for( size_t i = t; i < 4096; i += 4 ) data.visit( i ) ;
                } ) ;
            }
            for( auto & worker : workers ) worker.join() ;
        }
        return data.check() ;
    }

    bool_t pool( void_t )
    {
        coverage data( 0, 256 ) ;
        std::atomic_size_t done { 0 } ;
        {
            motor::concurrent::thread_pool_t pool ;
            pool.init() ;
            for( size_t i = 0; i < 256; ++i )
            {
                pool.schedule( motor::concurrent::global_t::make_task(
                    [&, i]( motor::concurrent::task_t::task_funk_param_in_t )
                {
                    data.visit( i ) ;
                    done.fetch_add( 1 ) ;
                } ) ) ;
            }
            while( done.load() != 256 ) std::this_thread::yield() ;
            pool.shutdown() ;
        }
        return data.check() ;
    }

    bool_t parallel( size_t const begin, size_t const count )
    {
        coverage data( begin, begin + count ) ;
        motor::concurrent::parallel_for<size_t>( range_t( begin, begin + count ),
            [&]( range_t const & r ) { data.visit( r ) ; } ) ;
        return data.check() ;
    }

    bool_t nested( size_t const hardware )
    {
        // One inner loop per outer chunk exercises worker-side yielding.
        size_t const count = hardware * 128 ;
        std::atomic_bool ok { true } ;
        coverage outer( 0, count ) ;
        motor::concurrent::parallel_for<size_t>( range_t( count ),
            [&]( range_t const & r )
        {
            outer.visit( r ) ;
            if( !parallel( 7, count ) ) ok.store( false ) ;
        } ) ;
        return outer.check() && ok.load() ;
    }
}

static int run_tests( int argc, char ** argv )
{
    if( argc != 2 )
    {
        motor::log::global_t::error( "Usage: 00_threads_parallel_for threads|pool|ranges|remainder|nested" ) ;
        return 2 ;
    }
    motor::string_t const mode = argv[1] ;
    if( mode != "threads" && mode != "pool" && mode != "ranges" &&
        mode != "remainder" && mode != "nested" ) return 2 ;

    auto const hardware = motor::concurrent::thread_t::hardware_concurrency() ;
    if( hardware == 0 )
    {
        motor::log::global_t::error( "Motor currently requires hardware_concurrency() > 0." ) ;
        return 1 ;
    }
    motor::log::global_t::status<256>( "[concurrent] %s, hardware threads: %u", mode.c_str(), hardware ) ;
    bool ok = true ;
    {
        if( mode == "threads" ) ok = this_file::threads() ;
        else if( mode == "pool" ) ok = this_file::pool() ;
        else if( mode == "nested" ) ok = this_file::nested( hardware ) ;
        else if( mode == "remainder" )
        {
            for( size_t i = 0; i < 32; ++i )
            {
                ok = this_file::parallel( 17, size_t( hardware ) * 128 + 1 ) && ok ;
            }
        }
        else
        {
            for( size_t const count : { size_t( 0 ), size_t( 1 ), size_t( 99 ),
                size_t( 100 ), size_t( hardware ) * 128 } )
            {
                bool const result = this_file::parallel( 13, count ) ;
                if( result ) motor::log::global_t::status<128>( "range size %zu: PASS", count ) ;
                else motor::log::global_t::error<128>( "range size %zu: FAIL", count ) ;
                ok = result && ok ;
            }
        }
    }

    if( ok ) motor::log::global_t::status<128>( "[concurrent] %s: PASS", mode.c_str() ) ;
    else motor::log::global_t::error<128>( "[concurrent] %s: FAIL", mode.c_str() ) ;
    return ok ? 0 : 1 ;
}

int main( int argc, char ** argv )
{
    auto const result = run_tests( argc, argv ) ;
    motor::concurrent::global_t::deinit() ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return result != 0 ? result : ( clean ? 0 : 1 ) ;
}
