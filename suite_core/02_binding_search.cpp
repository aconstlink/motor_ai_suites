#include <motor/log/global.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#if defined( _MSC_VER )
#define SEARCH_NOINLINE __declspec(noinline)
#else
#define SEARCH_NOINLINE __attribute__((noinline))
#endif

namespace this_file
{
    using void_t = void ;
    using std::size_t ;
    constexpr size_t untouched = std::numeric_limits<size_t>::max() ;
    volatile std::uint64_t sink = 0 ;

    // Only standard containers and values participate in the searches.
    template< typename T >
    SEARCH_NOINLINE bool find_if_search( std::vector<T> const & values,
        T const & key, size_t & index ) noexcept
    {
        auto iter = std::find_if(values.begin(),values.end(),
            [&]( T const & value ) { return value == key ; }) ;
        if( iter == values.end() ) return false ;
        index = size_t(iter-values.begin()) ;
        return true ;
    }

    template< typename T >
    SEARCH_NOINLINE bool while_search( std::vector<T> const & values,
        T const & key, size_t & index ) noexcept
    {
        size_t i = size_t(-1) ;
        while( ++i < values.size() && values[i] != key ) ;
        if( i == values.size() ) return false ;
        index = i ;
        return true ;
    }

    template< typename T >
    struct query
    {
        T key ;
        bool found ;
        size_t index ;
    } ;

    template< typename T >
    using search_t = bool (*)( std::vector<T> const &, T const &, size_t & ) noexcept ;

    SEARCH_NOINLINE bool while_cached( std::vector<std::uint64_t> const & values,
        std::uint64_t const & key, size_t & index ) noexcept
    {
        auto const needle = key ;
        size_t i = size_t(-1) ;
        while( ++i < values.size() && values[i] != needle ) ;
        if( i == values.size() ) return false ;
        index = i ;
        return true ;
    }

    SEARCH_NOINLINE bool pointer_search( std::vector<std::uint64_t> const & values,
        std::uint64_t const & key, size_t & index ) noexcept
    {
        if( values.empty() ) return false ;
        auto const needle = key ;
        auto const first = values.data() ;
        auto const last = first+values.size() ;
        auto current = first ;
        while( current != last && *current != needle ) ++current ;
        if( current == last ) return false ;
        index = size_t(current-first) ;
        return true ;
    }

    template< size_t Width >
    SEARCH_NOINLINE bool unrolled_search( std::vector<std::uint64_t> const & values,
        std::uint64_t const & key, size_t & index ) noexcept
    {
        static_assert(Width >= 2 && Width <= 4) ;
        auto const needle = key ;
        size_t const count = values.size() ;
        size_t i = 0 ;
        // Each full block has one bounds check; preserve first-match semantics.
        while( count-i >= Width )
        {
            if( values[i] == needle ) { index = i ; return true ; }
            if( values[i+1] == needle ) { index = i+1 ; return true ; }
            if constexpr( Width >= 3 )
                if( values[i+2] == needle ) { index = i+2 ; return true ; }
            if constexpr( Width >= 4 )
                if( values[i+3] == needle ) { index = i+3 ; return true ; }
            i += Width ;
        }
        while( i < count && values[i] != needle ) ++i ;
        if( i == count ) return false ;
        index = i ;
        return true ;
    }

    template< typename T >
    struct strategy { char const * name ; search_t<T> search ; } ;

    template< typename T >
    auto strategies( void_t )
    {
        if constexpr( std::is_same_v<T,std::uint64_t> )
            return std::array<strategy<T>,7>{{{"find_if",find_if_search<T>},
                {"while",while_search<T>},{"while_cached",while_cached},
                {"pointer",pointer_search},{"unroll2",unrolled_search<2>},
                {"unroll3",unrolled_search<3>},{"unroll4",unrolled_search<4>}}} ;
        else
            return std::array<strategy<T>,2>{{{"find_if",find_if_search<T>},
                {"while",while_search<T>}}} ;
    }

    bool verify_uint64_edges( void_t )
    {
        // Every hit position, all tails modulo 2/3/4, duplicates, aliases and misses.
        for( size_t count = 0 ; count <= 33 ; ++count )
        {
            std::vector<std::uint64_t> values(count) ;
            for( size_t i = 0 ; i < count ; ++i ) values[i] = std::uint64_t(i) ;
            if( count > 1 ) values.back() = values.front() ;
            if( count > 2 ) values[1] = std::numeric_limits<std::uint64_t>::max() ;
            for( size_t q = 0 ; q <= count ; ++q )
            {
                std::uint64_t const missing = std::uint64_t(count)+1000 ;
                auto const & key = q < count ? values[q] : missing ;
                size_t expected = untouched ;
                for( size_t i = 0 ; i < count ; ++i )
                    if( values[i] == key ) { expected = i ; break ; }
                for( auto const & entry : strategies<std::uint64_t>() )
                {
                    size_t index = untouched ;
                    if( entry.search(values,key,index) != (expected != untouched) || index != expected )
                        return false ;
                }
            }
        }
        return true ;
    }

    template< typename T, typename MakeValue >
    std::vector<query<T>> make_queries( std::vector<T> const & values,
        size_t const scenario, MakeValue make_value )
    {
        std::vector<query<T>> queries ;
        queries.reserve(256) ;
        std::uint32_t random = 0x12345678u ;
        for( size_t i = 0 ; i < 256 ; ++i )
        {
            random ^= random << 13 ; random ^= random >> 17 ; random ^= random << 5 ;
            size_t index = values.size() ;
            if( !values.empty() )
            {
                if( scenario == 0 ) index = 0 ;
                else if( scenario == 1 ) index = values.size()/2 ;
                else if( scenario == 2 ) index = values.size()-1 ;
                else if( scenario == 4 && i%4 != 0 ) index = random%values.size() ;
            }
            bool const found = index < values.size() ;
            queries.push_back({make_value(found ? index : values.size()+i),
                found,found ? index : untouched}) ;
        }
        return queries ;
    }

    template< typename T >
    bool verify( std::vector<T> const & values, std::vector<query<T>> const & queries )
    {
        for( auto const & q : queries )
        {
            for( auto const & entry : strategies<T>() )
            {
                size_t index = untouched ;
                if( entry.search(values,q.key,index) != q.found || index != q.index ) return false ;
            }
        }
        if( !values.empty() )
        {
            std::vector<T> const duplicates(4,values.front()) ;
            for( auto const & entry : strategies<T>() )
            {
                size_t index = untouched ;
                if( !entry.search(duplicates,values.front(),index) || index != 0 ) return false ;
            }
        }
        return true ;
    }

    struct measurement { double ns ; std::uint64_t checksum ; } ;

    template< typename T >
    SEARCH_NOINLINE measurement measure( search_t<T> search, std::vector<T> const & values,
        std::vector<query<T>> const & queries, size_t const iterations )
    {
        std::uint64_t checksum = 0 ;
        auto const start = std::chrono::steady_clock::now() ;
        for( size_t i = 0 ; i < iterations ; ++i )
        {
            size_t index = untouched ;
            bool const found = search(values,queries[i%queries.size()].key,index) ;
            checksum += found ? 1u+std::uint64_t(index) : 0u ;
        }
        auto const end = std::chrono::steady_clock::now() ;
        sink = checksum ;
        return {std::chrono::duration<double,std::nano>(end-start).count()/double(iterations),checksum} ;
    }

    template< typename T, typename MakeValue >
    bool run_type( char const * type, bool const verify_only, MakeValue make_value )
    {
        constexpr size_t sizes[] = {0,4,16,64,256} ;
        char const * const scenarios[] = {"first","middle","last","miss","mixed"} ;
        auto const searches = strategies<T>() ;
        for( auto const size : sizes )
        {
            std::vector<T> values ;
            values.reserve(size) ;
            for( size_t i = 0 ; i < size ; ++i ) values.push_back(make_value(i)) ;
            for( size_t scenario = 0 ; scenario < 5 ; ++scenario )
            {
                if( size == 0 && scenario != 3 ) continue ;
                auto const queries = make_queries(values,scenario,make_value) ;
                if( !verify(values,queries) ) return false ;
                if( verify_only ) continue ;
                size_t const iterations = std::max(size_t(50000),
                    200000/std::max(size_t(1),size/8)) ;
                std::uint64_t expected = 0 ;
                for( size_t i = 0 ; i < iterations ; ++i )
                {
                    auto const & q = queries[i%queries.size()] ;
                    expected += q.found ? 1u+std::uint64_t(q.index) : 0u ;
                }
                std::array<std::array<double,7>,std::tuple_size_v<decltype(searches)>> samples {} ;
                // Warm each variant once; then rotate the leading variant per round.
                for( auto const & entry : searches )
                    if( measure(entry.search,values,queries,iterations).checksum != expected ) return false ;
                for( size_t round = 0 ; round < 7 ; ++round )
                {
                    for( size_t offset = 0 ; offset < searches.size() ; ++offset )
                    {
                        size_t const which = (round+offset)%searches.size() ;
                        auto const result = measure(searches[which].search,values,queries,iterations) ;
                        if( result.checksum != expected ) return false ;
                        samples[which][round] = result.ns ;
                    }
                }
                for( auto & times : samples ) std::sort(times.begin(),times.end()) ;
                for( size_t which = 0 ; which < searches.size() ; ++which )
                    motor::log::global_t::status<512>("SEARCH_VARIANT,%s,%zu,%s,%zu,%s,%.3f,%.3f,%.3f",
                        type,size,scenarios[scenario],iterations,searches[which].name,
                        samples[which][3],samples[which].front(),samples[which].back()) ;
            }
        }
        return true ;
    }

    bool run( bool const verify_only, bool const uint64_only )
    {
        if( !verify_only )
        {
#ifdef NDEBUG
            motor::log::global_t::status("Standard-library search benchmark: optimized build; 1 warmup + 7 rounds.") ;
#else
            motor::log::global_t::warning("Debug build: use Release without debugger for performance comparisons.") ;
#endif
            motor::log::global_t::status("SEARCH_VARIANT_HEADER,type,count,case,iterations,strategy,median_ns,min_ns,max_ns") ;
        }
        bool ok = verify_uint64_edges() ;
        if( !uint64_only )
        {
            for( size_t const length : {size_t(9),size_t(64)} )
            {
                ok &= run_type<std::string>(length == 9 ? "string9" : "string64",verify_only,
                    [length]( size_t const i )
                {
                    auto suffix = std::to_string(i) ;
                    suffix.insert(0,4-suffix.size(),'0') ;
                    return std::string(length-4,'a')+suffix ;
                }) ;
            }
            ok &= run_type<std::uint32_t>("uint32",verify_only,
                []( size_t const i ) { return std::uint32_t(i)*37u+11u ; }) ;
        }
        ok &= run_type<std::uint64_t>("uint64",verify_only,
            []( size_t const i ) { return (std::uint64_t(1)<<40)+std::uint64_t(i)*37u+11u ; }) ;
        return ok ;
    }
}

int main( int argc, char ** argv )
{
    bool valid = true, verify_only = false, uint64_only = false ;
    for( int i = 1 ; i < argc ; ++i )
    {
        std::string const arg(argv[i]) ;
        if( arg == "--verify-only" ) verify_only = true ;
        else if( arg == "--uint64-only" ) uint64_only = true ;
        else valid = false ;
    }
    bool const ok = valid && this_file::run(verify_only,uint64_only) ;
    if( !valid ) motor::log::global_t::status("Usage: 02_binding_search [--verify-only] [--uint64-only]") ;
    motor::log::global_t::test("[standard search] results, indices, duplicates and checksums",ok) ;
    motor::log::global_t::deinit() ;
    return ok ? 0 : 1 ;
}
