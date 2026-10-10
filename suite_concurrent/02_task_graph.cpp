#include <motor/concurrent/global.h>
#include <motor/concurrent/task/loose_thread_scheduler.hpp>
#include <motor/concurrent/thread_pool.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <atomic>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;
    using task_t = motor::concurrent::task_t ;

    bool_t run( bool_t const pooled, size_t const width )
    {
        std::atomic_size_t roots { 0 }, joins { 0 }, tails { 0 } ;
        std::atomic_bool ok { true } ;
        motor::vector<std::atomic_size_t> visits( width ) ;
        for( auto & visit : visits ) visit.store( 0 ) ;
        motor::vector<motor::concurrent::task_mtr_safe_t> nodes ;
        nodes.emplace_back( motor::concurrent::global_t::make_task( [&]( task_t::task_funk_param_in_t )
        {
            roots.fetch_add( 1 ) ;
        } ) ) ;
        for( size_t i = 0; i < width; ++i )
        {
            nodes.emplace_back( motor::concurrent::global_t::make_task( [&, i]( task_t::task_funk_param_in_t )
            {
                if( roots.load() != 1 ) ok.store( false ) ;
                visits[i].fetch_add( 1 ) ;
            } ) ) ;
        }
        nodes.emplace_back( motor::concurrent::global_t::make_task( [&]( task_t::task_funk_param_in_t )
        {
            for( auto & visit : visits ) if( visit.load() != 1 ) ok.store( false ) ;
            joins.fetch_add( 1 ) ;
        } ) ) ;
        nodes.emplace_back( motor::concurrent::global_t::make_task( [&]( task_t::task_funk_param_in_t )
        {
            if( joins.load() != 1 ) ok.store( false ) ;
            tails.fetch_add( 1 ) ;
        } ) ) ;
        {
            for( size_t i = 1; i <= width; ++i )
            {
                nodes[0]->then( motor::share( nodes[i] ) ) ;
                nodes[i]->then( motor::share( nodes[width + 1] ) ) ;
            }
            nodes[width + 1]->then( motor::share( nodes.back() ) ) ;
        }
        if( pooled )
        {
            motor::concurrent::thread_pool_t pool ;
            pool.init() ;
            motor::log::global_t::status( "[graph] pool initialized; scheduling root" ) ;
            pool.schedule( motor::share( nodes[0] ) ) ;
            while( tails.load() == 0 ) std::this_thread::yield() ;
            motor::log::global_t::status( "[graph] terminal task reached; shutting down pool" ) ;
            pool.shutdown() ;
            motor::log::global_t::status( "[graph] pool shutdown complete" ) ;
        }
        else
        {
            motor::concurrent::loose_thread_scheduler_t scheduler ;
            scheduler.init() ;
            scheduler.schedule( motor::share( nodes[0] ) ) ;
            while( tails.load() == 0 )
            {
                scheduler.update() ;
                std::this_thread::yield() ;
            }
            scheduler.deinit() ;
        }
        // Stop workers before disconnecting the bidirectional, ref-counted graph.
        for( auto & node : nodes ) node->disconnect() ;
        for( auto & node : nodes ) motor::release( motor::move( node ) ) ;
        for( auto & visit : visits ) if( visit.load() != 1 ) ok.store( false ) ;
        return ok.load() && roots.load() == 1 && joins.load() == 1 && tails.load() == 1 ;
    }
}

int main( int argc, char ** argv )
{
    int result = 2 ;
    {
        motor::string_t const scheduler = argc == 3 ? argv[1] : "" ;
        motor::string_t const shape = argc == 3 ? argv[2] : "" ;
        size_t const width = shape == "chain" ? 1 : shape == "diamond" ? 2 : shape == "fanout" ? 12 : 0 ;
        if( (scheduler == "pool" || scheduler == "loose") && width != 0 )
        {
            bool const ok = this_file::run( scheduler == "pool", width ) ;
            result = ok ? 0 : 1 ;
            if( ok ) motor::log::global_t::status<256>( "[graph] %s/%s: PASS", scheduler.c_str(), shape.c_str() ) ;
            else motor::log::global_t::error<256>( "[graph] %s/%s: FAIL", scheduler.c_str(), shape.c_str() ) ;
        }
        else motor::log::global_t::error( "Usage: 02_task_graph pool|loose chain|diamond|fanout" ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
