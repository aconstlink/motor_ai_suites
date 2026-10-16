#include "test_runner.hpp"
#include <motor/log/system/system.h>
#include <motor/std/vector>
#include <utility>

namespace this_file
{
    using namespace motor::core::types ;
    using level_t = motor::log::log_level ;
    using result_t = motor::log::result ;

    class recorder : public motor::log::ilogger
    {
    public:
        struct entry { level_t level ; motor::string_t message ; } ;
        motor::vector<entry> entries ;
        result_t log( level_t const level, motor::string_cref_t message ) noexcept override
        {
            entries.push_back({level,message}) ;
            return result_t::ok ;
        }
        result_t log( level_t const level, char_cptr_t message ) noexcept override
        {
            return log(level,motor::string_t(message)) ;
        }
    } ;

    bool_t fanout( void_t )
    {
        recorder first, second ;
        motor::log::system_t system ;
        bool_t ok = system.add_logger(nullptr) == result_t::invalid_argument ;
        ok = (system.add_logger(&first) == result_t::ok) && ok ;
        ok = (system.add_logger(&second) == result_t::ok) && ok ;
        first.entries.clear() ; second.entries.clear() ;
        ok = (system.add_logger(&first) == result_t::ok) && ok ;
        system.log(level_t::warning,"fanout-marker") ;
        for( auto const * sink : {&first,&second} )
        {
            ok = (sink->entries.size() == 1 && sink->entries[0].level == level_t::warning &&
                sink->entries[0].message == "fanout-marker") && ok ;
        }
        return ok ;
    }

    bool_t test_result( void_t )
    {
        recorder sink ;
        motor::log::system_t system ;
        system.add_logger(&sink) ; sink.entries.clear() ;
        system.test("pass-marker",true) ;
        system.test("fail-marker",false) ;
        return sink.entries.size() == 2 &&
            sink.entries[0].level == level_t::status && sink.entries[0].message == "[OK] pass-marker" &&
            sink.entries[1].level == level_t::error && sink.entries[1].message == "[FAIL] fail-marker" ;
    }

    bool_t default_logger( void_t )
    {
        recorder sink ;
        motor::log::system_t system ;
        bool_t ok = system.set_default_logger(&sink) == nullptr ;
        system.log(level_t::warning,"replacement-marker") ;
        ok = (sink.entries.size() == 1 && sink.entries[0].message == "replacement-marker") && ok ;
        ok = (system.set_default_logger(nullptr) == &sink) && ok ;
        size_t const count = sink.entries.size() ;
        system.log(level_t::status,"restored-default-marker") ;
        return ok && sink.entries.size() == count ;
    }

    bool_t move( void_t )
    {
        motor::log::system_t source ;
        source.log(level_t::status,"before-move") ;
        motor::log::system_t destination(std::move(source)) ;
        auto const old_count = source.get_store()->get_num_items() ;
        auto const new_count = destination.get_store()->get_num_items() ;
        destination.log(level_t::status,"after-move") ;
        // Keep source alive to detect misrouting without dereferencing freed memory.
        return destination.get_store()->get_num_items() == new_count+1 &&
            source.get_store()->get_num_items() == old_count ;
    }
}

int main( int argc, char ** argv )
{
    log_tests::test_case const cases[] = {
        {"fanout",this_file::fanout}, {"test_result",this_file::test_result},
        {"default",this_file::default_logger}, {"move",this_file::move}
    } ;
    return log_tests::run(argc,argv,cases) ;
}
