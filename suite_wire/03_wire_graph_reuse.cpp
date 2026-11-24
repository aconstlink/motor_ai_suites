#include <motor/wire/node/node.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <semaphore>

namespace
{
    using node = motor::wire::funk_node_t ;
    using input = motor::wire::input_slot<unsigned> ;
    using output = motor::wire::output_slot<unsigned> ;

    node * make_node( char const * name )
    {
        return motor::shared( node( name, []( node * ) {} ) ) ;
    }

    input * add_input( node * n )
    {
        input * s = motor::shared( input( 0u ) ) ;
        n->inputs().add( motor::share_unsafe( s ) ) ;
        return s ;
    }

    output * add_output( node * n )
    {
        output * s = motor::shared( output( 0u ) ) ;
        n->outputs().add( motor::share_unsafe( s ) ) ;
        return s ;
    }

    bool run()
    {
        constexpr unsigned runs = 1000, change_at = runs / 2 ;
        std::counting_semaphore<> merged{ 0 } ;
        std::atomic_uint merges{ 0 }, seen_left{ 0 }, seen_right{ 0 } ;

        auto * root = make_node( "Clock" ) ;
        auto * left = make_node( "Even" ) ;
        auto * right = make_node( "Odd" ) ;
        auto * merger = make_node( "Merge" ) ;
        auto * clock = add_input( root ) ;
        auto * time = add_output( root ) ;
        output * timeline = motor::shared( output( 0u ) ) ;
        output * clip = motor::shared( output( 0u ) ) ;
        bool ok = clock->connect( motor::share( timeline ) ) ;
        root->set_funk( [=]( node * ) { time->set_value( clock->get_value() ) ; } ) ;

        auto branch = [&]( node * n, unsigned offset )
        {
            auto * t = add_input( n ) ;
            auto * value = add_output( n ) ;
            auto * result = add_input( merger ) ;
            ok &= time->connect( motor::share( t ) ) ;
            ok &= value->connect( motor::share( result ) ) ;
            root->then( motor::share( n ) ) ;
            n->then( motor::share( merger ) ) ;
            n->set_funk( [=]( node * ) { value->set_value( 2u * t->get_value() + offset ) ; } ) ;
            return result ;
        } ;
        auto * even = branch( left, 0 ) ;
        auto * odd = branch( right, 1 ) ;
        merger->set_funk( [&]( node * )
        {
            seen_left = even->get_value() ;
            seen_right = odd->get_value() ;
            ++merges ;
            merged.release() ;
        } ) ;

        auto end = merger->get_task() ;
        if( !ok ) std::fprintf( stderr, "Could not connect graph slots\n" ) ;
        for( unsigned i = 0; ok && i < runs; ++i )
        {
            unsigned const t = i < change_at ? i + 1 : i - change_at ;
            if( i < change_at ) timeline->set_and_exchange( t ) ;
            else
            {
                if( i == change_at && !clock->connect( motor::share( clip ) ) )
                {
                    std::fprintf( stderr, "Could not switch to clip clock at run %u\n", i + 1 ) ;
                    ok = false ;
                    break ;
                }
                clip->set_and_exchange( t ) ;
                // Write the old source last: a stale subscriber would receive the wrong tick.
                timeline->set_and_exchange( 10000u + i ) ;
            }

            motor::concurrent::global_t::schedule( root->get_task(), motor::concurrent::schedule_type::pool ) ;
            if( !merged.try_acquire_for( std::chrono::seconds( 5 ) ) )
            {
                std::fprintf( stderr, "Timed out waiting for merger at run %u\n", i + 1 ) ;
                ok = false ;
                break ;
            }
            end->wait_until_executed() ;
            ok = merges == i + 1 && seen_left == 2u * t && seen_right == 2u * t + 1 ;
            if( !ok ) std::fprintf( stderr, "Run %u: expected (%u,%u), got (%u,%u), merges=%u\n",
                i + 1, 2u * t, 2u * t + 1, seen_left.load(), seen_right.load(), merges.load() ) ;
        }

        motor::concurrent::global_t::deinit() ;
        ok &= merges == runs ;
        motor::release( motor::move( end ) ) ;
        output::release( motor::move( timeline ) ) ;
        output::release( motor::move( clip ) ) ;
        for( auto * n : {root, left, right, merger} ) n->disconnect() ;
        for( auto * n : {root, left, right, merger} ) motor::release( motor::move( n ) ) ;
        return ok ;
    }
}

int main()
{
    bool const ok = run() ;
    motor::log::global_t::test( "[wire] 1000 diamond runs; timeline -> clip after 500", ok ) ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
