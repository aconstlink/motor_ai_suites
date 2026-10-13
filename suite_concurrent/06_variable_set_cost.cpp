#include "cost_support.hpp"
#include <motor/memory/global.h>
#include <motor/std/vector>
#include <motor/graphics/variable/variable_set.hpp>
#include <latch>

namespace this_file
{
    using namespace motor::core::types ;
    using variable = motor::graphics::data_variable<motor::math::vec4f_t> ;
    struct measurement { double_t ms ; bool_t ok ; } ;

    // A common opaque load keeps the pointer baseline from collapsing into a
    // precomputed sum. Its call overhead is included in every scenario.
    COST_NOINLINE float_t read( variable const * ptr ) noexcept { return ptr->get().x() ; }

    measurement run( size_t const size, size_t const iterations, size_t const workers,
        size_t const mode, bool_t const write )
    {
        motor::graphics::variable_set_t vars ;
        motor::vector<motor::string_t> names ;
        motor::vector<variable *> cached ;
        for( size_t i = 0 ; i < size ; ++i )
        {
            names.emplace_back( "shader_value_" + motor::to_string( i ) ) ;
            auto * ptr = vars.data_variable<motor::math::vec4f_t>( names.back() ) ;
            ptr->set( motor::math::vec4f_t( float_t( i + 1 ), 0.0f, 0.0f, 1.0f ) ) ;
            cached.emplace_back( ptr ) ;
        }
        // This is only a primitive-cost control, not the private lock inside vars.
        motor::concurrent::mrsw_t control_lock ;
        double_t sums[2] = {} ;
        double_t expected_warmup = 0.0 ;
        for( size_t i = 0 ; i < 500 ; ++i ) expected_warmup += write ? i % 97 : i % size + 1 ;
        bool_t valid[2] = { true, true } ;
        std::latch ready( workers ), start( 1 ) ;
        motor::vector<motor::concurrent::thread_t> threads ;
        for( size_t w = 0 ; w < workers ; ++w )
        {
            threads.emplace_back( [&, w]( void_t )
            {
                double_t sum = 0.0 ;
                auto operation = [&]( size_t const i ) -> double_t
                {
                    size_t const index = i % size ;
                    variable * ptr = mode == 0 ? vars.find_data_variable<motor::math::vec4f_t>( names[index].c_str() ) : cached[index] ;
                    if( !ptr ) { valid[w] = false ; return 0.0 ; }
                    if( write ) ptr->set( motor::math::vec4f_t( float_t( i % 97 ), 0.0f, 0.0f, 1.0f ) ) ;
                    return read( ptr ) ;
                } ;
                for( size_t i = 0 ; i < 500 ; ++i )
                {
                    if( mode == 2 ) { motor::concurrent::mrsw_t::reader_lock_t lock( control_lock ) ; sum += operation( i ) ; }
                    else sum += operation( i ) ;
                }
                valid[w] &= sum == expected_warmup ;
                sum = 0.0 ; ready.count_down() ; start.wait() ;
                for( size_t i = 0 ; i < iterations ; ++i )
                {
                    if( mode == 2 ) { motor::concurrent::mrsw_t::reader_lock_t lock( control_lock ) ; sum += operation( i ) ; }
                    else sum += operation( i ) ;
                }
                sums[w] = sum ;
            } ) ;
        }
        ready.wait() ;
        auto const begin = cost_test::clock::now() ;
        start.count_down() ; for( auto & thread : threads ) thread.join() ;
        auto const ms = cost_test::elapsed_ms( begin ) ;
        double_t expected = 0.0 ;
        for( size_t i = 0 ; i < iterations ; ++i ) expected += write ? i % 97 : i % size + 1 ;
        bool_t ok = true ;
        for( size_t w = 0 ; w < workers ; ++w ) ok &= valid[w] && sums[w] == expected ;
        return { ms, ok } ;
    }
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    bool_t const bench = argc == 2 && motor::string_t( argv[1] ) == "--benchmark" ;
    if( argc > 2 || (argc == 2 && !bench && motor::string_t( argv[1] ) != "--smoke") ) return 2 ;
    size_t const iterations = bench ? 100000 : 2000, repetitions = bench ? 3 : 1 ;
    bool_t ok = true ;
    {
        cost_test::watchdog timeout ;
        char_cptr_t const labels[] = { "name_lookup", "cached_pointer", "mrsw_plus_cached_control" } ;
        for( auto const size : { size_t( 8 ), size_t( 64 ) } )
        {
            // Only immutable reads run concurrently. variable::set itself is not locked.
            for( size_t scenario = 0 ; scenario < 3 ; ++scenario )
            {
                size_t const workers = scenario == 1 ? 2 : 1 ;
                bool_t const write = scenario == 2 ;
                std::array<double_t, 3> times[3] = {} ;
                for( size_t rep = 0 ; rep < repetitions ; ++rep )
                {
                    for( size_t offset = 0 ; offset < 3 ; ++offset )
                    {
                        size_t const mode = (offset + rep) % 3 ;
                        auto const r = this_file::run( size, iterations, workers, mode, write ) ;
                        times[mode][rep] = r.ms ; ok &= r.ok ;
                    }
                }
                for( size_t mode = 0 ; mode < 3 ; ++mode ) cost_test::report(
                    "[varset] size=" + motor::to_string( size ) + " workers=" + motor::to_string( workers ) +
                    (write ? " set/get " : " read ") + labels[mode], times[mode], repetitions, iterations * workers ) ;
            }
        }
    }
    motor::log::global_t::test( "[varset] all checksums (no speed threshold)", ok ) ;
    motor::log::global_t::deinit() ;
    return motor::memory::global_t::dump_to_std() == 0 && ok ? 0 : 1 ;
}
