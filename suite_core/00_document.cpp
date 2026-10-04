#include <motor/core/document.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace this_file
{
    using namespace motor::core::types ;
    using tokens_t = motor::vector<motor::string_t> ;

    size_t count_tokens( std::string_view const text )
    {
        size_t count = 0 ;
        bool_t in_token = false ;
        for( char const c : text )
        {
            bool_t const whitespace = c == ' ' || c == '\t' || c == '\r' || c == '\n' ;
            if( !whitespace && !in_token ) ++count ;
            in_token = !whitespace ;
        }
        return count ;
    }

    template< typename F >
    void_t document_tokens( motor::string_in_t text, F && consume )
    {
        motor::core::document doc( text ) ;
        doc.for_each_token( [&]( size_t const, size_t const, std::string_view const & token )
        {
            consume( token ) ;
        } ) ;
    }

    // Standard-library baselines intentionally use std::string, not Motor's wrapper.
    template< typename F >
    void_t string_tokens( motor::string_in_t text, F && consume, bool_t const preallocate = false )
    {
        std::string const source( text.data(), text.size() ) ;
        std::vector<std::string> tokens ;
        if( preallocate ) tokens.reserve( count_tokens( source ) ) ;
        size_t start = source.find_first_not_of( " \t\r\n" ) ;
        while( start != std::string::npos )
        {
            size_t const end = source.find_first_of( " \t\r\n", start ) ;
            tokens.emplace_back( source.substr( start,
                end == std::string::npos ? end : end - start ) ) ;
            start = end == std::string::npos ? end : source.find_first_not_of( " \t\r\n", end ) ;
        }
        for( auto const & token : tokens ) consume( std::string_view( token ) ) ;
    }

    template< typename F >
    void_t stream_tokens( motor::string_in_t text, F && consume, bool_t const preallocate = false )
    {
        std::istringstream stream( std::string( text.data(), text.size() ) ) ;
        stream.imbue( std::locale::classic() ) ;
        std::vector<std::string> tokens ;
        if( preallocate ) tokens.reserve( count_tokens( std::string_view( text.data(), text.size() ) ) ) ;
        {
            std::string token ;
            while( stream >> token ) tokens.emplace_back( token ) ;
        }
        for( auto const & token : tokens ) consume( std::string_view( token ) ) ;
    }

    // A non-owning-token baseline separates the benefit of views from string allocation.
    template< typename F >
    void_t view_tokens( motor::string_in_t text, F && consume, bool_t const preallocate = false )
    {
        std::string const source( text.data(), text.size() ) ;
        std::string_view const view( source ) ;
        std::vector<std::string_view> tokens ;
        if( preallocate ) tokens.reserve( count_tokens( view ) ) ;
        size_t start = view.find_first_not_of( " \t\r\n" ) ;
        while( start != std::string_view::npos )
        {
            size_t const end = view.find_first_of( " \t\r\n", start ) ;
            tokens.emplace_back( view.substr( start,
                end == std::string_view::npos ? end : end - start ) ) ;
            start = end == std::string_view::npos ? end : view.find_first_not_of( " \t\r\n", end ) ;
        }
        for( auto const token : tokens ) consume( token ) ;
    }

    bool_t verify( char const * name, motor::string_in_t text, tokens_t const & expected )
    {
        bool_t ok = true ;
        auto test = [&]( char const * implementation, auto tokenize )
        {
            tokens_t actual ;
            tokenize( text, [&]( std::string_view const token )
            {
                actual.emplace_back( token.data(), token.size() ) ;
            } ) ;
            bool_t const matches = actual == expected ;
            if( matches ) motor::log::global_t::status<256>( "PASS: %s / %s", name, implementation ) ;
            else motor::log::global_t::error<256>( "FAIL: %s / %s: expected %zu tokens, got %zu",
                name, implementation, expected.size(), actual.size() ) ;
            ok = matches && ok ;
        } ;
        test( "document", []( auto const & t, auto && f ) { document_tokens( t, f ) ; } ) ;
        test( "std::string", []( auto const & t, auto && f ) { string_tokens( t, f ) ; } ) ;
        test( "stringstream", []( auto const & t, auto && f ) { stream_tokens( t, f ) ; } ) ;
        test( "string_view", []( auto const & t, auto && f ) { view_tokens( t, f ) ; } ) ;
        test( "std::string reserved", []( auto const & t, auto && f ) { string_tokens( t, f, true ) ; } ) ;
        test( "stringstream reserved", []( auto const & t, auto && f ) { stream_tokens( t, f, true ) ; } ) ;
        test( "string_view reserved", []( auto const & t, auto && f ) { view_tokens( t, f, true ) ; } ) ;
        return ok ;
    }

    bool_t correctness( motor::string_in_t scenario )
    {
        if( scenario == "basic" )
        {
            bool_t ok = verify( "empty", "", {} ) ;
            ok = verify( "single", "hello", { "hello" } ) && ok ;
            ok = verify( "LF", "alpha beta\ngamma delta\n", { "alpha", "beta", "gamma", "delta" } ) && ok ;
            ok = verify( "CRLF", "alpha beta\r\ngamma delta\r\n", { "alpha", "beta", "gamma", "delta" } ) && ok ;
            ok = verify( "tabs and spaces", "\t alpha  beta\t gamma  \n", { "alpha", "beta", "gamma" } ) && ok ;
            ok = verify( "punctuation", "vec3 p = (1,2,3);\n", { "vec3", "p", "=", "(1,2,3);" } ) && ok ;
            return ok ;
        }
        if( scenario == "blank_lines" )
            return verify( "blank lines", "\nalpha\n\n   \n\tbeta\n", { "alpha", "beta" } ) ;
        if( scenario == "final_line" )
            return verify( "indented final line without LF", "alpha\n  beta gamma", { "alpha", "beta", "gamma" } ) ;
        if( scenario == "whitespace" )
            return verify( "whitespace only", "   \t  ", {} ) ;
        motor::log::global_t::error( "Unknown scenario" ) ;
        return false ;
    }

    struct digest
    {
        std::uint64_t hash = 14695981039346656037ull ;
        size_t count = 0 ;
        void_t add( std::string_view const token )
        {
            for( unsigned char const c : token ) hash = ( hash ^ c ) * 1099511628211ull ;
            hash = ( hash ^ 255u ) * 1099511628211ull ;
            ++count ;
        }
    } ;

    motor::string_t benchmark_text( void_t )
    {
        motor::string_t text ;
        for( size_t i = 0 ; i != 8192 ; ++i )
        {
            text += "\t node_" ;
            text += motor::string_t( std::to_string( i ).c_str() ) ;
            text += "  position 1.25 -2.5 3.75 material_longer_than_small_string_storage\n" ;
        }
        return text ;
    }

    bool_t benchmark_traversal( void_t )
    {
        using benchmark_clock_t = std::chrono::steady_clock ;
        motor::string_t const text = benchmark_text() ;
        motor::core::document doc( text ) ;
        std::vector<std::string> strings ;
        std::vector<std::string> streamed_strings ;
        string_tokens( text, [&]( std::string_view t ) { strings.emplace_back( t ) ; } ) ;
        stream_tokens( text, [&]( std::string_view t ) { streamed_strings.emplace_back( t ) ; } ) ;

        // Views must outlive every measured traversal, together with their source.
        std::string const source( text.data(), text.size() ) ;
        std::string_view const view( source ) ;
        std::vector<std::string_view> views ;
        {
            size_t start = view.find_first_not_of( " \t\r\n" ) ;
            while( start != std::string_view::npos )
            {
                size_t const end = view.find_first_of( " \t\r\n", start ) ;
                views.emplace_back( view.substr( start,
                    end == std::string_view::npos ? end : end - start ) ) ;
                start = end == std::string_view::npos ? end : view.find_first_not_of( " \t\r\n", end ) ;
            }
        }

        digest expected ;
        for( auto const & token : strings ) expected.add( token ) ;
        using runner_t = std::function<digest ( void_t )> ;
        std::array<runner_t, 4> const runners = {
            [&]( void_t )
            {
                digest d ;
                doc.for_each_token( [&]( size_t const, size_t const, std::string_view const & t ) { d.add( t ) ; } ) ;
                return d ;
            },
            [&]( void_t ) { digest d ; for( auto const & t : strings ) d.add( t ) ; return d ; },
            [&]( void_t ) { digest d ; for( auto const & t : streamed_strings ) d.add( t ) ; return d ; },
            [&]( void_t ) { digest d ; for( auto const t : views ) d.add( t ) ; return d ; }
        } ;
        char const * names[] = { "document", "prebuilt substr tokens", "prebuilt stringstream tokens", "prebuilt string_view tokens" } ;
        std::array<std::array<double, 31>, 4> samples {} ;
        // One warmup round; all owners stay alive until after every measurement.
        for( size_t round = 0 ; round != 32 ; ++round )
        {
            for( size_t offset = 0 ; offset != runners.size() ; ++offset )
            {
                size_t const idx = ( round + offset ) % runners.size() ;
                auto const begin = benchmark_clock_t::now() ;
                auto const result = runners[idx]() ;
                double const ms = std::chrono::duration<double, std::milli>( benchmark_clock_t::now() - begin ).count() ;
                if( result.hash != expected.hash || result.count != expected.count )
                {
                    motor::log::global_t::error<256>( "Traversal token mismatch: %s", names[idx] ) ;
                    return false ;
                }
                if( round != 0 ) samples[idx][round - 1] = ms ;
            }
        }
        motor::log::global_t::status<256>( "Traversal only: %zu tokens; median of 31 runs; hash every character; no construction/destruction",
            expected.count ) ;
        for( size_t idx = 0 ; idx != runners.size() ; ++idx )
        {
            std::sort( samples[idx].begin(), samples[idx].end() ) ;
            motor::log::global_t::status<256>( "%s: %.3f ms per traversal", names[idx], samples[idx][15] ) ;
        }
        return true ;
    }

    bool_t benchmark( void_t )
    {
        using benchmark_clock_t = std::chrono::steady_clock ;
        motor::string_t const text = benchmark_text() ;
        digest expected ;
        string_tokens( text, [&]( std::string_view t ) { expected.add( t ) ; } ) ;
        using runner_t = digest (*)( motor::string_in_t ) ;
        std::array<runner_t, 7> const runners = {
            []( motor::string_in_t t ) { digest d ; document_tokens( t, [&]( auto v ) { d.add( v ) ; } ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; string_tokens( t, [&]( auto v ) { d.add( v ) ; } ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; stream_tokens( t, [&]( auto v ) { d.add( v ) ; } ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; view_tokens( t, [&]( auto v ) { d.add( v ) ; } ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; string_tokens( t, [&]( auto v ) { d.add( v ) ; }, true ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; stream_tokens( t, [&]( auto v ) { d.add( v ) ; }, true ) ; return d ; },
            []( motor::string_in_t t ) { digest d ; view_tokens( t, [&]( auto v ) { d.add( v ) ; }, true ) ; return d ; }
        } ;
        char const * names[] = { "document", "std::string + substr", "stringstream", "std::string_view",
            "std::string + count/reserve", "stringstream + count/reserve", "string_view + count/reserve" } ;
        std::array<std::array<double, 9>, 7> samples {} ;
        // Rotate order; first round warms up. Every timed result is checked and consumed.
        for( size_t round = 0 ; round != 10 ; ++round )
        {
            for( size_t offset = 0 ; offset != runners.size() ; ++offset )
            {
                size_t const idx = ( round + offset ) % runners.size() ;
                auto const begin = benchmark_clock_t::now() ;
                auto const result = runners[idx]( text ) ;
                double const ms = std::chrono::duration<double, std::milli>( benchmark_clock_t::now() - begin ).count() ;
                if( result.hash != expected.hash || result.count != expected.count )
                {
                    motor::log::global_t::error<256>( "Benchmark token mismatch: %s", names[idx] ) ;
                    return false ;
                }
                if( round != 0 ) samples[idx][round - 1] = ms ;
            }
        }
        motor::log::global_t::status<256>( "Bytes: %zu; tokens: %zu; median of 9 runs (copy, tokenize, hash, destroy)",
            text.size(), expected.count ) ;
        for( size_t idx = 0 ; idx != runners.size() ; ++idx )
        {
            std::sort( samples[idx].begin(), samples[idx].end() ) ;
            double const ms = samples[idx][4] ;
            motor::log::global_t::status<256>( "%s: %.3f ms, %.2f MiB/s", names[idx], ms,
                double( text.size() ) / ( 1024.0 * 1024.0 ) / ( ms / 1000.0 ) ) ;
        }
        return true ;
    }

    bool_t benchmark_counting( void_t )
    {
        motor::string_t const text = benchmark_text() ;
        std::string_view const view( text.data(), text.size() ) ;
        using counter_t = size_t (*)( std::string_view ) ;
        std::array<counter_t, 2> const counters = {
            &count_tokens,
            []( std::string_view t ) -> size_t { return size_t( std::count( t.begin(), t.end(), '\n' ) ) ; }
        } ;
        size_t const expected[] = { 49152, 8192 } ;
        char const * names[] = { "Exact token count (whitespace transitions)", "LF count (document line reserve strategy)" } ;
        std::array<std::array<double, 31>, 2> samples {} ;
        for( size_t round = 0 ; round != 32 ; ++round )
        {
            for( size_t offset = 0 ; offset != counters.size() ; ++offset )
            {
                size_t const idx = ( round + offset ) % counters.size() ;
                auto const begin = std::chrono::steady_clock::now() ;
                size_t const result = counters[idx]( view ) ;
                double const ms = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - begin ).count() ;
                if( result != expected[idx] )
                {
                    motor::log::global_t::error( "Counting benchmark mismatch" ) ;
                    return false ;
                }
                if( round != 0 ) samples[idx][round - 1] = ms ;
            }
        }
        for( size_t idx = 0 ; idx != counters.size() ; ++idx )
        {
            std::sort( samples[idx].begin(), samples[idx].end() ) ;
            motor::log::global_t::status<256>( "%s: %.3f ms (count only, median of 31)", names[idx], samples[idx][15] ) ;
        }
        return true ;
    }
}

int main( int argc, char ** argv )
{
    bool ok = false ;
    {
        motor::string_t const scenario = argc > 1 ? argv[1] : "basic" ;
        if( scenario == "--benchmark" ) ok = this_file::benchmark() ;
        else if( scenario == "--benchmark-traversal" ) ok = this_file::benchmark_traversal() ;
        else if( scenario == "--benchmark-counting" ) ok = this_file::benchmark_counting() ;
        else ok = this_file::correctness( scenario ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
