#include "cost_support.hpp"
#include <motor/memory/global.h>
#include <motor/std/vector>
#include <motor/platform/graphics/datas.hpp>
#include <latch>

namespace this_file
{
    using namespace motor::core::types ;
    constexpr size_t entries = 256 ;
    struct payload
    {
        uint64_t count = 0, sum = 0 ;
        void_t invalidate( motor::string_in_t ) noexcept { count = sum = 0 ; }
    } ;
    struct command { size_t index = 0 ; uint64_t sequence = 0 ; } ;
    using batch_t = std::array<command, 32> ;
    using datas_t = motor::platform::datas<payload> ;

    // A deliberately ordinary, bounded mutex/CV queue. This is a comparison
    // model, not a lock-free queue or a replacement for Motor's command system.
    class queue
    {
        std::mutex _mutex ;
        std::condition_variable _readable, _writable ;
        std::array<command, 256> _items ;
        size_t _head = 0, _tail = 0, _size = 0 ;
        bool_t _done = false ;
    public:
        size_t pushes = 0, pops = 0, high_water = 0 ;
        void_t push( command const & value )
        {
            std::unique_lock<std::mutex> lock( _mutex ) ;
            _writable.wait( lock, [this]( void_t ) { return _size < _items.size() ; } ) ;
            _items[_tail] = value ; _tail = (_tail + 1) % _items.size() ; ++_size ;
            ++pushes ; high_water = std::max( high_water, _size ) ;
            lock.unlock() ; _readable.notify_one() ;
        }
        size_t pop( batch_t & batch, size_t const limit, bool_t const wait )
        {
            std::unique_lock<std::mutex> lock( _mutex ) ; ++pops ;
            if( wait ) _readable.wait( lock, [this]( void_t ) { return _size != 0 || _done ; } ) ;
            size_t const count = std::min( limit, _size ) ;
            for( size_t i = 0 ; i < count ; ++i )
            { batch[i] = _items[_head] ; _head = (_head + 1) % _items.size() ; }
            _size -= count ;
            lock.unlock() ; if( count != 0 ) _writable.notify_one() ;
            return count ;
        }
        void_t finish( void_t )
        {
            { std::lock_guard<std::mutex> lock( _mutex ) ; _done = true ; }
            _readable.notify_one() ;
        }
    } ;

    COST_NOINLINE uint64_t read( payload const & p ) noexcept { return p.count + p.sum ; }
    struct measurement
    {
        double_t ms ;
        bool_t ok ;
        uint64_t observed ;
        size_t pops, high_water ;
    } ;

    measurement run( size_t const rounds, size_t const updates_per_round, size_t const mode )
    {
        bool_t const messaging = mode != 0, flat = mode >= 3 ;
        size_t const batch_size = mode == 2 || mode == 4 ? 32 : 1 ;
        size_t const updates = rounds * updates_per_round ;
        datas_t data ;
        std::array<size_t, entries> ids ;
        std::array<payload, entries> owned ;
        for( size_t i = 0 ; i < entries ; ++i )
        {
            ids[i] = size_t( -1 ) ;
            data.access( ids[i], "entry_" + motor::to_string( i ), []( payload & ) { return true ; } ) ;
        }
        queue messages ;
        std::latch ready( 2 ), start( 1 ) ;
        uint64_t observed = 0 ;
        uint64_t applied_sequence = 0 ;
        bool_t producer_ok = true, consumer_ok = true ;
        auto apply = [&]( command const & cmd )
        {
            // Exactly one thread applies in each topology; also verify FIFO/no duplicates.
            bool_t const ordered = cmd.sequence == ++applied_sequence ;
            auto update = [&]( payload & p ) { ++p.count ; p.sum += cmd.sequence ; } ;
            if( flat ) update( owned[cmd.index] ) ;
            else return data.access( ids[cmd.index], update ) && ordered ;
            return ordered ;
        } ;
        motor::concurrent::thread_t producer( [&]( void_t )
        {
            ready.count_down() ; start.wait() ;
            for( size_t i = 0 ; i < updates ; ++i )
            {
                command const cmd { i % entries, uint64_t( i + 1 ) } ;
                if( messaging ) messages.push( cmd ) ;
                else producer_ok = apply( cmd ) && producer_ok ;
            }
            if( messaging ) messages.finish() ;
        } ) ;
        motor::concurrent::thread_t consumer( [&]( void_t )
        {
            batch_t batch ;
            auto drain = [&]( bool_t const wait )
            {
                size_t const count = messages.pop( batch, batch_size, wait ) ;
                for( size_t i = 0 ; i < count ; ++i ) consumer_ok = apply( batch[i] ) && consumer_ok ;
                return count ;
            } ;
            ready.count_down() ; start.wait() ;
            for( size_t round = 0 ; round < rounds ; ++round )
            {
                if( messaging ) drain( false ) ;
                for( size_t i = 0 ; i < entries ; ++i )
                {
                    if( flat ) observed += read( owned[i] ) ;
                    else consumer_ok = data.access( ids[i], [&]( payload & p ) { observed += read( p ) ; } ) && consumer_ok ;
                }
            }
            // End-to-end time includes applying the tail, not just enqueuing it.
            if( messaging ) while( drain( true ) != 0 ) {}
        } ) ;
        ready.wait() ;
        auto const begin = cost_test::clock::now() ;
        start.count_down() ; producer.join() ; consumer.join() ;
        double_t const ms = cost_test::elapsed_ms( begin ) ;
        std::array<payload, entries> expected ;
        for( size_t i = 0 ; i < updates ; ++i ) { ++expected[i % entries].count ; expected[i % entries].sum += i + 1 ; }
        bool_t ok = producer_ok && consumer_ok && applied_sequence == updates ;
        for( size_t i = 0 ; i < entries ; ++i )
        {
            auto check = [&]( payload & p ) { ok &= p.count == expected[i].count && p.sum == expected[i].sum ; } ;
            if( flat ) check( owned[i] ) ;
            else ok = data.access( ids[i], check ) && ok ;
        }
        if( messaging ) ok &= messages.pushes == updates && messages.high_water <= 256 ;
        return { ms, ok, observed, messages.pops, messages.high_water } ;
    }
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    bool_t const bench = argc == 2 && motor::string_t( argv[1] ) == "--benchmark" ;
    if( argc > 2 || (argc == 2 && !bench && motor::string_t( argv[1] ) != "--smoke") ) return 2 ;
    size_t const rounds = bench ? 1000 : 40, repetitions = bench ? 3 : 1 ;
    bool_t ok = true ;
    {
        cost_test::watchdog timeout ;
        char_cptr_t const labels[] = { "shared_datas", "queue_datas_batch1", "queue_datas_batch32",
            "queue_owned_batch1", "queue_owned_batch32" } ;
        for( size_t const update_rate : { size_t( 1 ), size_t( 16 ) } )
        {
            std::array<double_t, 3> times[5] = {} ;
            // Warm each complete topology outside the recorded trials.
            for( size_t mode = 0 ; mode < 5 ; ++mode ) ok = this_file::run( 8, update_rate, mode ).ok && ok ;
            for( size_t rep = 0 ; rep < repetitions ; ++rep )
            {
                for( size_t offset = 0 ; offset < 5 ; ++offset )
                {
                    size_t const mode = (offset + rep) % 5 ;
                    auto const r = this_file::run( rounds, update_rate, mode ) ;
                    times[mode][rep] = r.ms ; ok &= r.ok ;
                    // Observation sums intentionally differ: message visibility is delayed.
                    motor::log::global_t::status( "[owner] " + motor::string_t( labels[mode] ) +
                        " observed=" + motor::to_string( r.observed ) + " pop_calls=" + motor::to_string( r.pops ) +
                        " queue_high_water=" + motor::to_string( r.high_water ) ) ;
                }
            }
            for( size_t mode = 0 ; mode < 5 ; ++mode ) cost_test::report(
                "[owner] reads=" + motor::to_string( rounds * this_file::entries ) + " updates=" +
                motor::to_string( rounds * update_rate ) + " " + labels[mode], times[mode], repetitions,
                rounds * (this_file::entries + update_rate) ) ;
        }
    }
    motor::log::global_t::test( "[owner] every update applied, final per-entry counts and sequence sums", ok ) ;
    motor::log::global_t::deinit() ;
    return motor::memory::global_t::dump_to_std() == 0 && ok ? 0 : 1 ;
}
