#include <motor/core/document.hpp>
#include <motor/io/database.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace this_file
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;
    using time_t = clock_t::time_point ;

    double_t milliseconds( time_t const a, time_t const b ) noexcept
    { return std::chrono::duration<double_t, std::milli>( b - a ).count() ; }

    struct digest
    {
        std::uint64_t hash = 14695981039346656037ull ;
        size_t tokens = 0 ;
        size_t bytes = 0 ;

        void_t add( std::string_view const token ) noexcept
        {
            for( unsigned char const c : token ) hash = ( hash ^ c ) * 1099511628211ull ;
            hash = ( hash ^ 255u ) * 1099511628211ull ;
            ++tokens ; bytes += token.size() ;
        }

        bool_t operator == ( digest const & rhs ) const noexcept
        { return hash == rhs.hash && tokens == rhs.tokens && bytes == rhs.bytes ; }
    } ;

    template< typename F >
    void_t scan( std::string_view const text, F && consume )
    {
        size_t start = text.find_first_not_of( " \t\r\n" ) ;
        while( start != std::string_view::npos )
        {
            size_t const end = text.find_first_of( " \t\r\n", start ) ;
            size_t const length = end == std::string_view::npos ? text.size() - start : end - start ;
            consume( start, length ) ;
            start = end == std::string_view::npos ? end : text.find_first_not_of( " \t\r\n", end ) ;
        }
    }

    // The baselines deliberately use the standard allocator, as in 00_document.
    struct string_tokens
    {
        std::string source ;
        std::vector<std::string> tokens ;

        string_tokens( motor::string_in_t text, bool_t const reserve, bool_t const stream )
            : source( text.data(), text.size() )
        {
            if( reserve )
            {
                size_t count = 0 ;
                scan( source, [&]( size_t, size_t ) { ++count ; } ) ;
                tokens.reserve( count ) ;
            }
            if( stream )
            {
                std::istringstream input( source ) ;
                input.imbue( std::locale::classic() ) ;
                std::string token ;
                while( input >> token ) tokens.emplace_back( token ) ;
            }
            else
            {
                scan( source, [&]( size_t const start, size_t const length )
                { tokens.emplace_back( source.substr( start, length ) ) ; } ) ;
            }
        }
    } ;

    struct view_tokens
    {
        std::string source ;
        std::vector<std::string_view> tokens ;

        view_tokens( motor::string_in_t text, bool_t const reserve )
            : source( text.data(), text.size() )
        {
            std::string_view const input( source.data(), source.size() ) ;
            if( reserve )
            {
                size_t count = 0 ;
                scan( input, [&]( size_t, size_t ) { ++count ; } ) ;
                tokens.reserve( count ) ;
            }
            scan( input, [&]( size_t const start, size_t const length )
            { tokens.emplace_back( input.substr( start, length ) ) ; } ) ;
        }

        view_tokens( view_tokens const & ) = delete ;
        view_tokens( view_tokens && ) = delete ;
    } ;

    digest traverse( view_tokens const & data )
    {
        digest d ;
        for( auto const & token : data.tokens ) d.add( token ) ;
        return d ;
    }

    digest traverse( motor::core::document & doc )
    {
        digest d ;
        doc.for_each_token( [&]( size_t const, size_t const, std::string_view const & token )
        { d.add( token ) ; } ) ;
        return d ;
    }

    digest traverse( string_tokens const & data )
    {
        digest d ;
        for( auto const & token : data.tokens ) d.add( token ) ;
        return d ;
    }

    struct sample
    {
        double_t build = 0.0 ;
        double_t walk = 0.0 ;
        double_t destroy = 0.0 ;
        double_t total = 0.0 ;
        digest value ;
    } ;

    template< typename F >
    sample measure( F && make )
    {
        sample s ;
        time_t built, walked ;
        auto const begin = clock_t::now() ;
        {
            auto data = make() ;
            built = clock_t::now() ;
            s.value = traverse( data ) ;
            walked = clock_t::now() ;
        }
        auto const end = clock_t::now() ;
        s.build = milliseconds( begin, built ) ;
        s.walk = milliseconds( built, walked ) ;
        s.destroy = milliseconds( walked, end ) ;
        s.total = milliseconds( begin, end ) ;
        return s ;
    }

    bool_t run( motor::string_in_t location, bool_t const verify_only )
    {
        motor::string_t text ;
        {
            auto const begin = clock_t::now() ;
            motor::io::database_t db( motor::io::path_t( DATAPATH ), "./working", "data" ) ;
            auto const initialized = clock_t::now() ;
            bool_t loaded = false ;
            bool_t const waited = db.load( motor::io::location_t( location ) ).wait_for_operation(
                [&]( char_cptr_t data, size_t const size, motor::io::result const result )
                {
                    loaded = result == motor::io::result::ok && data != nullptr && size != 0 ;
                    if( loaded ) text.assign( data, size ) ;
                } ) ;
            auto const end = clock_t::now() ;
            if( !waited || !loaded )
            {
                motor::log::global_t::error( motor::string_t( "Could not load OBJ from database: " ) + location ) ;
                return false ;
            }
            motor::log::global_t::status<512>( "Database: %s/working; location: %s", DATAPATH, location.c_str() ) ;
            motor::log::global_t::status<256>( "Database init: %.3f ms; load + wait + copy: %.3f ms; bytes: %zu",
                milliseconds( begin, initialized ), milliseconds( initialized, end ), text.size() ) ;
        }

        // Avoid overflowing document's current fixed-size line scratch buffer.
        {
            size_t length = 0 ;
            for( char_t const c : text )
            {
                if( c == '\n' ) length = 0 ;
                else if( ++length >= 4096 )
                {
                    motor::log::global_t::error( "OBJ line reaches document's 4096-byte scratch-buffer limit" ) ;
                    return false ;
                }
            }
        }

        // Independent, allocation-free reference outside the measured tokenizers.
        digest expected ;
        std::string_view const input( text.data(), text.size() ) ;
        scan( input, [&]( size_t const start, size_t const size ) { expected.add( input.substr( start, size ) ) ; } ) ;
        {
            motor::core::document doc( text ) ;
            digest by_line ;
            size_t lines = 0, positions = 0, normals = 0, uvs = 0, faces = 0 ;
            doc.for_each_line( [&]( motor::core::document::line_view const & line )
            {
                ++lines ;
                auto const count = line.get_num_tokens() ;
                if( count == 0 ) return ;
                auto const keyword = line.get_token( 0 ) ;
                positions += keyword == "v" ; normals += keyword == "vn" ;
                uvs += keyword == "vt" ; faces += keyword == "f" ;
                for( size_t i = 0 ; i < count ; ++i ) by_line.add( line.get_token( i ) ) ;
            } ) ;
            if( !( by_line == expected ) || !( traverse( doc ) == expected ) )
            {
                motor::log::global_t::error( "Document token/line traversal differs from the whitespace reference" ) ;
                return false ;
            }
            motor::log::global_t::status<256>( "Document: %zu lines, %zu tokens; OBJ records: v=%zu vt=%zu vn=%zu f=%zu",
                lines, expected.tokens, positions, uvs, normals, faces ) ;
            motor::log::global_t::status( "PASS: for_each_line and for_each_token match the reference" ) ;
        }
        if( verify_only ) return true ;

        char_cptr_t const names[6] = { "document", "std::string + substr", "std::string + count/reserve + substr", "stringstream",
            "std::string_view", "std::string_view + count/reserve" } ;
        std::array<std::array<sample, 5>, 6> results {} ;
        for( size_t round = 0 ; round < 6 ; ++round )
        {
            for( size_t offset = 0 ; offset < results.size() ; ++offset )
            {
                size_t const idx = ( round + offset ) % results.size() ;
                sample s ;
                if( idx == 0 ) s = measure( [&] { return motor::core::document( text ) ; } ) ;
                else if( idx < 4 ) s = measure( [&] { return string_tokens( text, idx == 2, idx == 3 ) ; } ) ;
                else s = measure( [&] { return view_tokens( text, idx == 5 ) ; } ) ;
                if( !( s.value == expected ) )
                {
                    motor::log::global_t::error<256>( "FAIL: token checksum mismatch: %s", names[idx] ) ;
                    return false ;
                }
                if( round != 0 ) results[idx][round - 1] = s ;
            }
        }
        motor::log::global_t::status( "Median of 5 runs after warmup; order rotated; all times in ms; IO excluded" ) ;
        for( size_t idx = 0 ; idx < results.size() ; ++idx )
        {
            auto median = [&]( double_t sample::* member )
            {
                std::array<double_t, 5> values ;
                for( size_t i = 0 ; i < 5 ; ++i ) values[i] = results[idx][i].*member ;
                std::sort( values.begin(), values.end() ) ;
                return values[2] ;
            } ;
            motor::log::global_t::status<512>( "%s: build %.3f; traversal+hash %.3f; destroy %.3f; total %.3f; %.2f MiB/s",
                names[idx], median( &sample::build ), median( &sample::walk ), median( &sample::destroy ),
                median( &sample::total ), double_t( text.size() ) / ( 1024.0 * 1024.0 ) / ( median( &sample::total ) / 1000.0 ) ) ;
        }
        return true ;
    }
}

int main( int argc, char ** argv )
{
    bool ok = false ;
    {
        motor::string_t location = "sponza.obj" ;
        bool verify_only = false ;
        for( int i = 1 ; i < argc ; ++i )
        {
            motor::string_t const arg( argv[i] ) ;
            if( arg == "--verify-only" ) verify_only = true ;
            else if( arg == "--location" && i + 1 < argc ) location = argv[++i] ;
            else
            {
                motor::log::global_t::status( "Usage: 01_document_obj [--verify-only] [--location sponza.obj]" ) ;
                motor::log::global_t::deinit() ;
                return arg == "--help" ? 0 : 2 ;
            }
        }
        ok = this_file::run( location, verify_only ) ;
    }
    motor::io::global_t::deinit() ;
    motor::concurrent::global_t::deinit() ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
