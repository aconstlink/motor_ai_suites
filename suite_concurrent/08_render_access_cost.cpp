#include "cost_support.hpp"
#include <motor/graphics/object.h>
#include <motor/graphics/variable/variable_set.hpp>
#include <motor/graphics/shader/compilation_listener.h>
#include <motor/memory/global.h>
#include <latch>

namespace this_file
{
    using namespace motor::core::types ;
    using object = motor::graphics::object ;
    using variable = motor::graphics::data_variable<motor::math::vec4f_t> ;
    COST_NOINLINE size_t read_id( size_t const * ptr ) noexcept { return *ptr ; }
    COST_NOINLINE size_t read_value( variable const * ptr ) noexcept { return size_t( ptr->get().x() ) ; }
    COST_NOINLINE size_t read_texture( motor::graphics::texture_variable_t const * ptr ) noexcept
    { return ptr != nullptr ? 1 : 0 ; }

    struct fixture
    {
        object obj ;
        motor::graphics::variable_set_t vars ;
        motor::graphics::compilation_listener_t listener ;
        motor::concurrent::mrsw_t lock ;
        size_t id = 7 ;
        variable * value = nullptr ;
        motor::graphics::texture_variable_t * texture = nullptr ;
        fixture( void_t )
        {
            obj.set_oid( 0, id ) ;
            object::data_manipulator( &obj, 0 ).change_to_ready( motor::graphics::result::ok ) ;
            for( size_t i = 0 ; i < 16 ; ++i )
                vars.data_variable<motor::math::vec4f_t>( "value_" + motor::to_string( i ) )->set( motor::math::vec4f_t( 7.0f ) ) ;
            value = vars.find_data_variable<motor::math::vec4f_t>( "value_15" ) ;
            for( size_t i = 0 ; i < 8 ; ++i ) texture = vars.texture_variable( "texture_" + motor::to_string( i ) ) ;
        }
        COST_NOINLINE size_t operation( size_t const mode )
        {
            switch( mode )
            {
            case 0: return read_id( &id ) ;
            case 1: { motor::concurrent::mrsw_t::reader_lock_t guard( lock ) ; return read_id( &id ) ; }
            case 2: return obj.get_oid( 0 ) ;
            case 3: return object::data_manipulator( &obj, 0 ).get_status().first == motor::graphics::object_state::ready ? 7 : 0 ;
            case 4: return listener.has_changed() ? 0 : 7 ;
            case 5: return read_value( value ) ;
            case 6: return read_value( vars.find_data_variable<motor::math::vec4f_t>( "value_15" ) ) ;
            case 7: return read_texture( texture ) * 7 ;
            case 8: return read_texture( vars.find_texture_variable( "texture_7" ) ) * 7 ;
            case 9: return obj.get_oid( 0 ) + obj.get_oid( 0 ) + obj.get_oid( 0 ) + obj.get_oid( 0 ) ;
            default: return read_id( &id ) + read_id( &id ) + read_id( &id ) + read_id( &id ) ;
            }
        }
    } ;
}

int main( int argc, char ** argv )
{
    using namespace motor::core::types ;
    bool_t const bench = argc == 2 && motor::string_t( argv[1] ) == "--benchmark" ;
    if( argc > 2 || (argc == 2 && !bench && motor::string_t( argv[1] ) != "--smoke") ) return 2 ;
    size_t const iterations = bench ? 200000 : 2000, repeats = bench ? 3 : 1 ;
    bool_t ok = true ;
    {
        cost_test::watchdog timeout ;
        this_file::fixture data ;
        char_cptr_t labels[] = { "snapshot_id", "mrsw_control", "get_oid", "get_status", "listener",
            "cached_value", "find_value_16", "cached_texture", "find_texture_8", "four_get_oid", "four_snapshot_id" } ;
        for( size_t const workers : { size_t( 1 ), size_t( 2 ) } )
        {
            std::array<double_t, 3> times[11] = {} ;
            for( size_t rep = 0 ; rep < repeats ; ++rep )
            {
                for( size_t offset = 0 ; offset < 11 ; ++offset )
                {
                    size_t const mode = (offset + rep * 4) % 11 ;
                    std::latch ready( workers ), start( 1 ) ;
                    motor::vector<motor::concurrent::thread_t> threads ;
                    size_t sums[2] = {} ;
                    for( size_t w = 0 ; w < workers ; ++w ) threads.emplace_back( [&, w]( void_t )
                    {
                        size_t sum = 0 ;
                        for( size_t i = 0 ; i < 500 ; ++i ) sum += data.operation( mode ) ;
                        ready.count_down() ; start.wait() ;
                        for( size_t i = 0 ; i < iterations ; ++i ) sum += data.operation( mode ) ;
                        sums[w] = sum ;
                    } ) ;
                    ready.wait() ; auto const begin = cost_test::clock::now() ; start.count_down() ;
                    for( auto & thread : threads ) thread.join() ;
                    times[mode][rep] = cost_test::elapsed_ms( begin ) ;
                    for( size_t w = 0 ; w < workers ; ++w ) ok &= sums[w] == (iterations + 500) * (mode >= 9 ? 28 : 7) ;
                }
            }
            for( size_t mode = 0 ; mode < 11 ; ++mode ) cost_test::report(
                "[render-access] workers=" + motor::to_string( workers ) + " " + labels[mode],
                times[mode], repeats, iterations * workers ) ;
        }
    }
    motor::log::global_t::test( "[render-access] immutable reads and checksums", ok ) ;
    motor::log::global_t::deinit() ;
    return motor::memory::global_t::dump_to_std() == 0 && ok ? 0 : 1 ;
}
