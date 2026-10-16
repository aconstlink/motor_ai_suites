#pragma once

#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>

namespace log_tests
{
    using namespace motor::core::types ;

    struct test_case
    {
        char_cptr_t name ;
        bool_t (*run)( void_t ) ;
        bool_t isolated = false ;
    } ;

    template< size_t N >
    int run( int argc, char ** argv, test_case const (&cases)[N] )
    {
        int result = 2 ;
        {
            motor::log::global_t::init() ;
            motor::string_t const mode = argc == 1 ? "--smoke" : argv[1] ;
            bool_t const smoke = mode == "--smoke" ;
            bool_t ok = true ;
            size_t executed = 0 ;
            if( argc <= 2 )
            {
                for( auto const & test : cases )
                {
                    if( smoke ? test.isolated : mode != test.name ) continue ;
                    bool_t const passed = test.run() ;
                    motor::log::global_t::test(motor::string_t("[log] ")+test.name,passed) ;
                    ok = passed && ok ;
                    ++executed ;
                }
            }
            if( executed ) result = ok ? 0 : 1 ;
            else
            {
                motor::log::global_t::error("Pass --smoke or one scenario name:") ;
                for( auto const & test : cases ) motor::log::global_t::status(test.name) ;
            }
        }
        motor::log::global_t::deinit() ;
        return motor::memory::global_t::dump_to_std() == 0 ? result : 1 ;
    }
}
