#pragma once

#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <cmath>
#include <limits>

namespace math_test
{
    using namespace motor::core::types ;
    constexpr float_t pi = 3.14159265358979323846f ;

    struct context
    {
        size_t checks = 0, failures = 0 ;
        motor::string_t section ;

        void_t check( bool_t const ok, char_cptr_t name )
        {
            ++checks ;
            if( ok ) return ;
            ++failures ;
            motor::log::global_t::error<512>("FAIL: %s%s",section.c_str(),name) ;
        }

        void_t near( double_t const actual, double_t const expected,
            char_cptr_t name, double_t const tolerance = 2.0e-5 )
        {
            bool_t const ok = std::isfinite(actual) && std::isfinite(expected) &&
                std::abs(actual-expected) <= tolerance * (1.0+std::abs(expected)) ;
            ++checks ;
            if( ok ) return ;
            ++failures ;
            motor::log::global_t::error<512>("FAIL: %s%s: expected %.12g, got %.12g",section.c_str(),name,expected,actual) ;
        }

        template< size_t N, typename A, typename B >
        void_t elements( A const & actual, B const & expected, char_cptr_t name )
        {
            for( size_t i = 0 ; i < N ; ++i )
            {
                motor::string_t const item = motor::string_t(name)+" ["+motor::to_string(i)+"]" ;
                near(double_t(actual[i]),double_t(expected[i]),item.c_str()) ;
            }
        }
    } ;

    struct test_case { char_cptr_t name ; void_t (*run)(context &) ; } ;

    template< size_t N >
    int_t run( int_t const argc, char ** argv, test_case const (&cases)[N] )
    {
        int_t result = 0 ;
        {
            motor::string_t const selected = argc == 2 ? argv[1] : "all" ;
            context ctx ;
            size_t executed = 0 ;
            if( argc <= 2 ) for( auto const & test : cases )
            {
                if( selected != "all" && selected != test.name ) continue ;
                size_t const before = ctx.failures, checks = ctx.checks ;
                ctx.section.clear() ;
                test.run(ctx) ;
                ++executed ;
                motor::log::global_t::status<256>("%s: %s (%zu checks, %zu failures)",
                    before == ctx.failures ? "PASS" : "FAIL",test.name,ctx.checks-checks,ctx.failures-before) ;
            }
            if( executed == 0 )
            {
                motor::log::global_t::error("Expected no argument, 'all', or one of:") ;
                for( auto const & test : cases ) motor::log::global_t::status(test.name) ;
                result = 2 ;
            }
            else result = ctx.failures == 0 ? 0 : 1 ;
        }
        motor::log::global_t::deinit() ;
        if( motor::memory::global_t::dump_to_std() != 0 ) result = 1 ;
        return result ;
    }
}
