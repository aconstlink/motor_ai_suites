#include "test_runner.hpp"
#include <motor/log/logger/store_logger.h>
#include <motor/std/vector>
#include <array>
#include <thread>
#include <utility>

namespace this_file
{
    using namespace motor::core::types ;
    using store_t = motor::log::store_logger_t ;
    using level_t = motor::log::log_level ;

    motor::string_t message( size_t const i )
    {
        return motor::string_t("entry-")+motor::to_string(i) ;
    }

    void_t fill( store_t & store, size_t const count )
    {
        for( size_t i = 0 ; i < count ; ++i )
            store.log(i % 2 ? level_t::warning : level_t::status,message(i)) ;
    }

    bool_t sequence( store_t const & store, size_t const first, size_t const count )
    {
        if( store.get_num_items() != count ) return false ;
        size_t read = 0 ;
        bool_t ok = true ;
        store.for_each(0,count,[&]( store_t::store_data_cref_t item )
        {
            size_t const expected = first+read++ ;
            // Short circuit avoids reading the level of a bogus empty entry.
            ok = (item.msg == message(expected) &&
                item.ll == (expected % 2 ? level_t::warning : level_t::status)) && ok ;
        }) ;
        return ok && read == count ;
    }

    bool_t empty( void_t )
    {
        store_t store ;
        size_t visits = 0 ;
        store.for_each(0,0,[&]( store_t::store_data_cref_t ) { ++visits ; }) ;
        motor::vector<store_t::store_data_t> copy ;
        store.copy(0,0,copy) ;
        return store.get_num_items() == 0 && store.get_max_items() > 0 && visits == 0 && copy.empty() ;
    }

    bool_t order( void_t )
    {
        store_t store ;
        fill(store,6) ;
        return sequence(store,0,6) ;
    }

    bool_t wrap( void_t )
    {
        store_t store ;
        size_t const capacity = store.get_max_items() ;
        fill(store,capacity*2+7) ;
        motor::log::global_t::status<128>("[log] wrap: count=%zu, capacity=%zu",store.get_num_items(),capacity) ;
        return sequence(store,capacity+7,capacity) ;
    }

    bool_t bounded( void_t )
    {
        store_t store ;
        motor::string_t const text(256,'x') ;
        size_t const capacity = store.get_max_items() ;
        for( size_t i = 0 ; i < capacity*2 ; ++i ) store.log(level_t::status,text) ;
        // Isolated process, direct store, fixed payload; no global logging or workers
        // between these measurements. This measures retained motor-tracked bytes.
        auto const before = motor::memory::global_t::get_sib() ;
        for( size_t i = 0 ; i < capacity*6 ; ++i ) store.log(level_t::status,text) ;
        auto const after = motor::memory::global_t::get_sib() ;
        motor::log::global_t::status<160>("[log] bounded: retained bytes after warmup=%zu, after more writes=%zu",before,after) ;
        return after <= before ;
    }

    bool_t copy_zero( void_t )
    {
        store_t store ;
        fill(store,6) ;
        motor::vector<store_t::store_data_t> copy ;
        store.copy(0,3,copy) ;
        if( copy.size() != 3 ) return false ;
        for( size_t i = 0 ; i < copy.size() ; ++i )
            if( copy[i].msg != message(i) ) return false ;
        return true ;
    }

    bool_t copy_range( void_t )
    {
        store_t store ;
        fill(store,6) ;
        motor::vector<store_t::store_data_t> copy ;
        // Intentionally a dedicated process: the current implementation writes
        // past the target for nonzero begin. CTest reports crashes as failures.
        store.copy(2,5,copy) ;
        if( copy.size() != 3 ) return false ;
        for( size_t i = 0 ; i < copy.size() ; ++i )
            if( copy[i].msg != message(i+2) ) return false ;
        return true ;
    }

    bool_t copy_wrap( void_t )
    {
        store_t store ;
        size_t const capacity = store.get_max_items() ;
        fill(store,capacity*2+7) ;
        motor::vector<store_t::store_data_t> copy ;
        store.copy(0,3,copy) ;
        if( copy.size() != 3 ) return false ;
        for( size_t i = 0 ; i < copy.size() ; ++i )
            if( copy[i].msg != message(capacity+7+i) ) return false ;
        return true ;
    }

    bool_t moved( void_t )
    {
        store_t original ;
        fill(original,4) ;
        store_t moved(std::move(original)) ;
        store_t assigned ;
        assigned = std::move(moved) ;
        assigned.log(level_t::status,message(4)) ;
        return sequence(assigned,0,5) ;
    }

    bool_t writers( void_t )
    {
        store_t store ;
        constexpr size_t threads = 4, per_thread = 64 ;
        std::array<std::thread,threads> workers ;
        for( size_t t = 0 ; t < threads ; ++t )
        {
            workers[t] = std::thread([&,t]( void_t )
            {
                for( size_t i = 0 ; i < per_thread ; ++i )
                    store.log(level_t::status,message(t*per_thread+i)) ;
            }) ;
        }
        for( auto & worker : workers ) worker.join() ;
        // Read only after joining: does not assume concurrent reader safety.
        if( store.get_num_items() != threads*per_thread ) return false ;
        std::array<bool_t,threads*per_thread> seen{} ;
        bool_t ok = true ;
        store.for_each(0,threads*per_thread,[&]( store_t::store_data_cref_t entry )
        {
            size_t id = 0 ;
            while( id < seen.size() && entry.msg != message(id) ) ++id ;
            if( id == seen.size() || seen[id] ) ok = false ;
            else seen[id] = true ;
        }) ;
        for( auto const present : seen ) ok = present && ok ;
        return ok ;
    }
}

int main( int argc, char ** argv )
{
    log_tests::test_case const cases[] = {
        {"empty",this_file::empty}, {"order",this_file::order},
        {"wrap",this_file::wrap}, {"bounded",this_file::bounded},
        {"copy_zero",this_file::copy_zero}, {"copy_range",this_file::copy_range,true},
        {"copy_wrap",this_file::copy_wrap,true}, {"move",this_file::moved},
        {"writers",this_file::writers}
    } ;
    return log_tests::run(argc,argv,cases) ;
}
