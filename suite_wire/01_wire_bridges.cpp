#include <motor/graphics/variable/variable_set.hpp>
#if __has_include(<motor/graphics/variable/wire_variable_input_bridge.hpp>)
#define MOTOR_SAMPLE_NEW_BRIDGES 1
#include <motor/graphics/variable/wire_variable_input_bridge.hpp>
#include <motor/graphics/variable/wire_variable_output_bridge.hpp>
#else
#define MOTOR_SAMPLE_NEW_BRIDGES 0
#include <motor/graphics/variable/wire_variable_bridge.h>
#endif
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>

namespace this_file
{
    using namespace motor::core::types ;
    using slot_t = motor::wire::input_slot<float_t> ;
    using output_t = motor::wire::output_slot<float_t> ;
#if MOTOR_SAMPLE_NEW_BRIDGES
    using input_bridge_t = motor::graphics::wire_variable_input_bridge_t ;
#else
    using input_bridge_t = motor::graphics::wire_variable_bridge_t ;
#endif

    auto & inputs( input_bridge_t & bridge )
    {
#if MOTOR_SAMPLE_NEW_BRIDGES
        return bridge.borrow_inputs() ;
#else
        return *bridge.borrow_inputs() ;
#endif
    }

    bool_t check( bool_t const value, char const * message )
    {
        if( value ) motor::log::global_t::status<256>( "PASS: %s", message ) ;
        else motor::log::global_t::error<256>( "FAIL: %s", message ) ;
        return value ;
    }

    bool_t input_test( void_t )
    {
        auto vs = motor::shared( motor::graphics::variable_set_t() ) ;
        auto * var = vs->data_variable<float_t>( "value" ) ;
        var->set( 1.0f ) ;
        input_bridge_t bridge( motor::move( vs ) ) ;
        bridge.update_bindings() ;
        auto * slot = inputs( bridge ).borrow_by_cast<slot_t>( "value" ) ;
        if( !check( slot != nullptr, "input slot created" ) ) return false ;
        bool_t ok = check( slot->get_value() == 1.0f, "slot initialized from variable" ) ;
        auto source = motor::shared( output_t( 4.0f ) ) ;
        ok = check( slot->connect( motor::share( source ) ), "connect source" ) && ok ;
        source->set_and_exchange( 8.0f ) ;
        ok = check( var->get() == 1.0f, "variable waits for bridge pull" ) && ok ;
        bridge.pull_data() ;
        ok = check( var->get() == 8.0f, "input bridge updates variable" ) && ok ;
        output_t::release( motor::move( source ) ) ;
        return ok ;
    }

#if MOTOR_SAMPLE_NEW_BRIDGES
    bool_t output_test( bool_t const rebind )
    {
        auto base = motor::shared( motor::graphics::variable_set_t() ) ;
        auto * value = base->data_variable<float_t>( "value" ) ;
        value->set( 1.0f ) ;
        motor::graphics::wire_variable_output_bridge_t output( motor::move( base ) ) ;
        auto * source = output.borrow_outputs().borrow_by_cast<output_t>( "value" ) ;
        if( !check( source != nullptr, "output slot created" ) ) return false ;

        auto first = motor::shared( motor::graphics::variable_set_t() ) ;
        auto second = motor::shared( motor::graphics::variable_set_t() ) ;
        auto * a = first->data_variable<float_t>( "value" ) ;
        auto * b = second->data_variable<float_t>( "value" ) ;
        a->set( -1.0f ) ;
        b->set( -2.0f ) ;
        input_bridge_t one( motor::move( first ) ) ;
        input_bridge_t two( motor::move( second ) ) ;
        auto * ia = inputs( one ).borrow_by_cast<slot_t>( "value" ) ;
        auto * ib = inputs( two ).borrow_by_cast<slot_t>( "value" ) ;
        if( !check( ia != nullptr && ib != nullptr, "subset slots created" ) ) return false ;
        bool_t ok = check( ia->connect( motor::share( source ) ), "connect subset one" ) ;
        ok = check( ib->connect( motor::share( source ) ), "connect subset two" ) && ok ;
        one.pull_data() ;
        two.pull_data() ;
        ok = check( a->get() == 1.0f && b->get() == 1.0f, "initial values reach variables" ) && ok ;

        value->set( 7.0f ) ;
        if( rebind )
        {
            output.update_bindings() ;
            ok = check( value->get() == 7.0f, "rebinding preserves source variable" ) && ok ;
        }
        output.pull_data() ;
        ok = check( source->get_value() == 7.0f && ia->get_value() == 1.0f,
            "pull updates output but not receivers" ) && ok ;
        output.exchange() ;
        one.pull_data() ;
        two.pull_data() ;
        ok = check( a->get() == 7.0f && b->get() == 7.0f, "both subset variables updated" ) && ok ;
        a->set( 99.0f ) ;
        ok = check( value->get() == 7.0f && b->get() == 7.0f, "sets own independent values" ) && ok ;
        output.clear() ;
        auto connection = ia->get_output_slot() ;
        ok = check( connection == nullptr, "clearing output disconnects subset" ) && ok ;
        motor::release( motor::move( connection ) ) ;
        ok = check( a->get() == 99.0f && b->get() == 7.0f, "subset variables survive source release" ) && ok ;
        return ok ;
    }
#endif

    int run( int argc, char ** argv )
    {
        if( argc != 2 ) return 2 ;
        motor::string_t const mode = argv[1] ;
        if( mode == "input" ) return input_test() ? 0 : 1 ;
        if( mode != "output" && mode != "rebind" ) return 2 ;
#if MOTOR_SAMPLE_NEW_BRIDGES
        return output_test( mode == "rebind" ) ? 0 : 1 ;
#else
        motor::log::global_t::warning( "SKIP: pinned Motor has no separate output bridge; update the submodule first." ) ;
        return 77 ;
#endif
    }
}

int main( int argc, char ** argv )
{
    int const result = this_file::run( argc, argv ) ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
