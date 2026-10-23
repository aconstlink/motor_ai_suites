#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <locale>
#include <memory>

#if defined( _MSC_VER )
#define COMPONENT_NOINLINE __declspec(noinline)
#else
#define COMPONENT_NOINLINE __attribute__((noinline))
#endif

namespace this_file
{
    using void_t = void ;
    using id_t = std::uint16_t ;
    using sum_t = std::uint64_t ;
    constexpr size_t queries = 5 ;
    volatile sum_t result_sink = 0 ;

    // Local fixtures only: no scene graph, backend, registry or engine changes.
    struct component
    {
        sum_t value ;
        explicit component( sum_t const v ) noexcept : value(v) {}
        virtual ~component( void_t ) = default ;
    } ;

    template< id_t Id >
    struct typed_component : component
    {
        explicit typed_component( sum_t const v ) noexcept : component(v) {}
    } ;

    struct entry { id_t type ; component * ptr ; } ;
    using result_t = std::array<component *, queries> ;
    struct node
    {
        motor::vector<std::unique_ptr<component>> owners ;
        motor::vector<component *> components ;
        motor::vector<entry> sorted ;
        result_t expected = {} ;
    } ;

    std::unique_ptr<component> make_component( id_t const id, sum_t const value )
    {
        switch( id )
        {
        case 0: return std::make_unique<typed_component<0>>(value) ;
        case 1: return std::make_unique<typed_component<1>>(value) ;
        case 2: return std::make_unique<typed_component<2>>(value) ;
        case 3: return std::make_unique<typed_component<3>>(value) ;
        case 4: return std::make_unique<typed_component<4>>(value) ;
        case 5: return std::make_unique<typed_component<5>>(value) ;
        case 6: return std::make_unique<typed_component<6>>(value) ;
        case 7: return std::make_unique<typed_component<7>>(value) ;
        case 8: return std::make_unique<typed_component<8>>(value) ;
        default: return std::make_unique<typed_component<9>>(value) ;
        }
    }

    std::uint32_t random( std::uint32_t & state ) noexcept
    {
        state ^= state << 13 ; state ^= state >> 17 ; state ^= state << 5 ;
        return state ;
    }

    template< typename T >
    void_t shuffle( T & values, std::uint32_t & state )
    {
        for( size_t i = values.size() ; i > 1 ; --i )
            std::swap(values[i-1],values[random(state)%i]) ;
    }

    node make_node( motor::vector<id_t> const & ids, sum_t const seed )
    {
        node n ;
        for( auto const id : ids )
        {
            n.owners.emplace_back(make_component(id,seed*17+id+1)) ;
            auto * p = n.owners.back().get() ;
            n.components.emplace_back(p) ;
            n.sorted.emplace_back(entry{id,p}) ;
            if( id%2 == 0 ) n.expected[id/2] = p ;
        }
        std::sort(n.sorted.begin(),n.sorted.end(),
            []( entry const & a, entry const & b ) { return a.type < b.type ; }) ;
        return n ;
    }

    template< id_t Id >
    component * find_rtti( node const & n ) noexcept
    {
        for( auto * p : n.components )
            if( auto * hit = dynamic_cast<typed_component<Id> *>(p) ) return hit ;
        return nullptr ;
    }

    result_t repeated_rtti( node const & n ) noexcept
    {
        return {{find_rtti<0>(n),find_rtti<2>(n),find_rtti<4>(n),
            find_rtti<6>(n),find_rtti<8>(n)}} ;
    }

    result_t repeated_id_linear( node const & n ) noexcept
    {
        result_t out = {} ;
        for( size_t q = 0 ; q < queries ; ++q )
        {
            id_t const wanted = id_t(q*2) ;
            for( auto const & e : n.sorted )
            {
                if( e.type != wanted ) continue ;
                out[q] = e.ptr ; break ;
            }
        }
        return out ;
    }

    result_t repeated_id_binary( node const & n ) noexcept
    {
        result_t out = {} ;
        for( size_t q = 0 ; q < queries ; ++q )
        {
            id_t const wanted = id_t(q*2) ;
            auto const it = std::lower_bound(n.sorted.begin(),n.sorted.end(),wanted,
                []( entry const & e, id_t const id ) { return e.type < id ; }) ;
            if( it != n.sorted.end() && it->type == wanted ) out[q] = it->ptr ;
        }
        return out ;
    }

    result_t single_pass_rtti( node const & n ) noexcept
    {
        result_t out = {} ;
        for( auto * p : n.components )
        {
            if( auto * hit0 = dynamic_cast<typed_component<0> *>(p) ) out[0] = hit0 ;
            else if( auto * hit1 = dynamic_cast<typed_component<2> *>(p) ) out[1] = hit1 ;
            else if( auto * hit2 = dynamic_cast<typed_component<4> *>(p) ) out[2] = hit2 ;
            else if( auto * hit3 = dynamic_cast<typed_component<6> *>(p) ) out[3] = hit3 ;
            else if( auto * hit4 = dynamic_cast<typed_component<8> *>(p) ) out[4] = hit4 ;
        }
        return out ;
    }

    result_t single_pass_id( node const & n ) noexcept
    {
        result_t out = {} ;
        for( auto const & e : n.sorted )
        {
            switch( e.type )
            {
            case 0: out[0] = e.ptr ; break ;
            case 2: out[1] = e.ptr ; break ;
            case 4: out[2] = e.ptr ; break ;
            case 6: out[3] = e.ptr ; break ;
            case 8: out[4] = e.ptr ; break ;
            default: break ;
            }
        }
        return out ;
    }

    sum_t consume( result_t const & out ) noexcept
    {
        sum_t sum = 0 ;
        for( size_t i = 0 ; i < out.size() ; ++i )
            sum += out[i] ? (out[i]->value ^ (sum_t(i+1)*0x9e3779b9u)) : sum_t(i+1) ;
        return sum ;
    }

    using search_t = result_t (*)( node const & ) noexcept ;
    using batch_t = sum_t (*)( motor::vector<node> const & ) noexcept ;

    // One non-inlined batch call per sweep; searches inline within the node loop.
    template< search_t Search >
    COMPONENT_NOINLINE sum_t batch( motor::vector<node> const & nodes ) noexcept
    {
        sum_t sum = 0 ;
        for( auto const & n : nodes ) sum += consume(Search(n)) ;
        return sum ;
    }

    struct strategy { char const * name ; search_t search ; batch_t run ; } ;
    std::array<strategy,5> const strategies = {{
        {"linear_rtti",repeated_rtti,batch<repeated_rtti>},
        {"linear_id",repeated_id_linear,batch<repeated_id_linear>},
        {"binary_id",repeated_id_binary,batch<repeated_id_binary>},
        {"single_pass_rtti",single_pass_rtti,batch<single_pass_rtti>},
        {"single_pass_id",single_pass_id,batch<single_pass_id>}
    }} ;

    bool verify( void_t )
    {
        std::uint32_t state = 0x734ac139u ;
        // All subsets of ten types with up to five components, including empty
        // nodes; four insertion orders each. Even IDs are the requested types.
        for( unsigned mask = 0 ; mask < 1024 ; ++mask )
        {
            motor::vector<id_t> ids ;
            for( id_t i = 0 ; i < 10 ; ++i ) if( mask & (1u<<i) ) ids.emplace_back(i) ;
            if( ids.size() > 5 ) continue ;
            for( size_t order = 0 ; order < 4 ; ++order )
            {
                shuffle(ids,state) ;
                auto const n = make_node(ids,mask+order) ;
                for( auto const & method : strategies )
                    if( method.search(n) != n.expected ) return false ;
            }
        }
        return true ;
    }

    motor::vector<node> make_nodes( size_t const count, size_t const scenario, size_t const total )
    {
        std::uint32_t state = 0x7183b271u ;
        motor::vector<node> nodes ; nodes.reserve(total) ;
        for( size_t i = 0 ; i < total ; ++i )
        {
            std::array<id_t,5> wanted = {{0,2,4,6,8}}, other = {{1,3,5,7,9}} ;
            shuffle(wanted,state) ; shuffle(other,state) ;
            size_t const hits = scenario == 0 ? count : scenario == 1 ? (count+i%2)/2 : 0 ;
            motor::vector<id_t> ids ;
            for( size_t j = 0 ; j < count ; ++j ) ids.emplace_back(j < hits ? wanted[j] : other[j-hits]) ;
            shuffle(ids,state) ;
            nodes.emplace_back(make_node(ids,i+1)) ;
        }
        shuffle(nodes,state) ;
        return nodes ;
    }

    struct options
    {
        size_t nodes = 4096, sweeps = 16, rounds = 7 ;
        bool verify_only = false ;
        motor::string_t csv ;
    } ;

    bool benchmark( options const & opts )
    {
        std::ofstream csv ;
        if( !opts.csv.empty() )
        {
            csv.open(opts.csv.c_str()) ;
            if( !csv ) return false ;
            csv.imbue(std::locale::classic()) ; csv << std::fixed << std::setprecision(6) ;
            csv << "components,scenario,nodes,queries_per_node,hit_percent,sweeps,round,strategy,ns_per_node,checksum\n" ;
        }
        motor::log::global_t::status("COMPONENT_HEADER,count,scenario,hit_percent,strategy,median_ns_per_node,min_ns,max_ns") ;
        std::array<char const *,3> const names = {{"wanted_only","mixed","all_miss"}} ;
        for( size_t count = 1 ; count <= 5 ; ++count )
        {
            for( size_t scenario = 0 ; scenario < names.size() ; ++scenario )
            {
                auto const nodes = make_nodes(count,scenario,opts.nodes) ;
                sum_t expected = 0 ; size_t hits = 0 ;
                for( auto const & n : nodes )
                {
                    expected += consume(n.expected) ;
                    for( auto * p : n.expected ) if( p ) ++hits ;
                    for( auto const & method : strategies )
                        if( method.search(n) != n.expected ) return false ;
                }
                double const hit_percent = 100.0*double(hits)/double(opts.nodes*queries) ;
                std::array<motor::vector<double>,5> samples ;
                for( auto & s : samples ) s.reserve(opts.rounds) ;
                for( size_t round = 0 ; round <= opts.rounds ; ++round )
                {
                    for( size_t order = 0 ; order < strategies.size() ; ++order )
                    {
                        size_t const index = (order+round+count+scenario)%strategies.size() ;
                        // Volatile function-pointer loads prevent folding repeated sweeps.
                        batch_t volatile run = strategies[index].run ;
                        sum_t sum = 0 ;
                        auto const begin = std::chrono::steady_clock::now() ;
                        for( size_t sweep = 0 ; sweep < opts.sweeps ; ++sweep ) sum += run(nodes) ;
                        auto const end = std::chrono::steady_clock::now() ;
                        result_sink = sum ;
                        if( sum != expected*opts.sweeps ) return false ;
                        double const ns = std::chrono::duration<double,std::nano>(end-begin).count()
                            /double(opts.nodes*opts.sweeps) ;
                        if( round == 0 ) continue ;
                        samples[index].emplace_back(ns) ;
                        if( csv.is_open() ) csv << count << ',' << names[scenario] << ',' << opts.nodes
                            << ',' << queries << ',' << hit_percent << ',' << opts.sweeps << ',' << round
                            << ',' << strategies[index].name << ',' << ns << ',' << sum << '\n' ;
                    }
                }
                for( size_t i = 0 ; i < strategies.size() ; ++i )
                {
                    auto & s = samples[i] ; std::sort(s.begin(),s.end()) ;
                    double const median = (s[(s.size()-1)/2]+s[s.size()/2])*0.5 ;
                    motor::log::global_t::status<256>("COMPONENT,%zu,%s,%.1f,%s,%.3f,%.3f,%.3f",
                        count,names[scenario],hit_percent,strategies[i].name,median,s.front(),s.back()) ;
                }
            }
        }
        if( csv.is_open() ) { csv.flush() ; return csv.good() ; }
        return true ;
    }

    bool parse( int const argc, char ** argv, options & opts )
    {
        for( int i = 1 ; i < argc ; ++i )
        {
            motor::string_t const arg = argv[i] ;
            if( arg == "--verify-only" ) { opts.verify_only = true ; continue ; }
            if( ++i == argc ) return false ;
            if( arg == "--csv" ) { opts.csv = argv[i] ; continue ; }
            motor::string_t const text = argv[i] ; size_t value = 0 ;
            auto const parsed = std::from_chars(text.data(),text.data()+text.size(),value) ;
            if( parsed.ec != std::errc() || parsed.ptr != text.data()+text.size() || value == 0 ) return false ;
            if( arg == "--nodes" && value <= 65536 ) opts.nodes = value ;
            else if( arg == "--sweeps" && value <= 4096 ) opts.sweeps = value ;
            else if( arg == "--rounds" && value <= 101 ) opts.rounds = value ;
            else return false ;
        }
        return true ;
    }
}

int main( int argc, char ** argv )
{
    bool ok = false ;
    {
        this_file::options opts ;
        bool const valid = this_file::parse(argc,argv,opts) ;
        if( !valid ) motor::log::global_t::status(
            "Usage: 03_component_search [--verify-only] [--csv file] [--nodes N] [--sweeps N] [--rounds N]") ;
        ok = valid && this_file::verify() ;
        motor::log::global_t::test("[component search] exact pointers, hits, misses and empty nodes",ok) ;
        if( ok && !opts.verify_only )
        {
#if defined( NDEBUG )
            motor::log::global_t::status("Optimized build. Five requested types per node; warmup + rotated rounds.") ;
#else
            motor::log::global_t::status("WARNING: non-Release build; do not use these timings for performance decisions.") ;
#endif
            ok = this_file::benchmark(opts) ;
            motor::log::global_t::test("[component search] all timed checksums",ok) ;
        }
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
