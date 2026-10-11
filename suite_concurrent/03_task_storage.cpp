#include <motor/concurrent/task/task.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

namespace this_file
{
    using namespace motor::core::types ;
    using task_t = motor::concurrent::task_t ;
    using nodes_t = motor::vector<motor::concurrent::task_mtr_t> ;

    nodes_t make_nodes( void_t )
    {
        nodes_t nodes ;
        for( size_t i = 0; i < 9; ++i )
        {
            nodes.emplace_back( motor::memory::global_t::create( task_t(), "task storage test" ) ) ;
        }
        return nodes ;
    }

    void_t connect( nodes_t const & nodes )
    {
        for( size_t i = 1; i < nodes.size(); ++i )
        {
            nodes[0]->then( motor::share( nodes[i] ) ) ;
        }
    }

    void_t destroy( nodes_t & nodes )
    {
        for( auto * node : nodes ) node->disconnect() ;
        for( auto * node : nodes ) motor::memory::release_ptr( node ) ;
        nodes.clear() ;
    }

    template< typename T >
    void_t release_entries( T & entries )
    {
        for( size_t i = 0; i < entries.size(); ++i )
        {
            motor::memory::release_ptr( entries[i] ) ;
        }
        entries.clear() ;
    }

    bool_t schedule( void_t )
    {
        auto nodes = make_nodes() ;
        connect( nodes ) ;
        bool_t ok = true ;
        {
            nodes_t scratch ;
            size_t warm_capacity = 0 ;
            size_t capacity_changes = 0 ;
            for( size_t iteration = 0; iteration < 10000; ++iteration )
            {
                size_t const before = scratch.capacity() ;
                // Keep an existing entry to check append semantics, not only empty reuse.
                scratch.emplace_back( motor::memory::copy_ptr( nodes[0] ) ) ;
                task_t::scheduler_accessor::schedule( nodes[0], scratch ) ;
                ok = (scratch.size() == nodes.size()) && ok ;
                if( scratch.size() == nodes.size() )
                {
                    for( size_t i = 0; i < nodes.size(); ++i ) ok = (scratch[i] == nodes[i]) && ok ;
                }
                if( iteration == 0 ) warm_capacity = scratch.capacity() ;
                else ok = (scratch.capacity() == warm_capacity) && ok ;
                if( scratch.capacity() != before ) ++capacity_changes ;
                release_entries( scratch ) ;
            }
            motor::log::global_t::status<256>(
                "[task storage] schedule: iterations=10000 warm_capacity=%zu final_capacity=%zu capacity_changes=%zu",
                warm_capacity, scratch.capacity(), capacity_changes ) ;
        }
        destroy( nodes ) ;
        return ok ;
    }

    bool_t disconnect( void_t )
    {
        auto nodes = make_nodes() ;
        bool_t ok = true ;
        {
            task_t::tasks_t scratch ;
            size_t warm_capacity = 0 ;
            size_t capacity_changes = 0 ;
            for( size_t iteration = 0; iteration < 256; ++iteration )
            {
                connect( nodes ) ;
                size_t const before = scratch.capacity() ;
                scratch.push_back( motor::memory::copy_ptr( nodes[0] ) ) ;
                // Drain both ends before releasing transferred edge references.
                for( auto * node : nodes ) task_t::cleaner_accessor::move_out_all( node, scratch ) ;
                size_t const expected = 1 + 2 * (nodes.size() - 1) ;
                ok = (scratch.size() == expected) && ok ;
                if( scratch.size() == expected )
                {
                    for( size_t i = 0; i < nodes.size(); ++i ) ok = (scratch[i] == nodes[i]) && ok ;
                    for( size_t i = nodes.size(); i < expected; ++i ) ok = (scratch[i] == nodes[0]) && ok ;
                }
                if( iteration == 0 ) warm_capacity = scratch.capacity() ;
                else ok = (scratch.capacity() == warm_capacity) && ok ;
                if( scratch.capacity() != before ) ++capacity_changes ;
                release_entries( scratch ) ;
            }
            motor::log::global_t::status<256>(
                "[task storage] disconnect: iterations=256 warm_capacity=%zu final_capacity=%zu capacity_changes=%zu",
                warm_capacity, scratch.capacity(), capacity_changes ) ;
        }
        destroy( nodes ) ;
        return ok ;
    }
}

int main( int argc, char ** argv )
{
    int result = 2 ;
    {
        motor::string_t const scenario = argc == 2 ? argv[1] : "" ;
        if( scenario == "schedule" || scenario == "disconnect" )
        {
            bool const ok = scenario == "schedule" ? this_file::schedule() : this_file::disconnect() ;
            result = ok ? 0 : 1 ;
            motor::log::global_t::test( "[task storage] " + scenario + " contents and stable capacity", ok ) ;
        }
        else motor::log::global_t::error( "Usage: 03_task_storage schedule|disconnect" ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
