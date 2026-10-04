#include <motor/wire/slot/sheet.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>

namespace this_file
{
    using namespace motor::core::types ;
    using input_t = motor::wire::input_slot<float_t> ;
    using output_t = motor::wire::output_slot<float_t> ;

    bool_t check( bool_t const value, char const * message )
    {
        if( value ) motor::log::global_t::status<256>( "PASS: %s", message ) ;
        else motor::log::global_t::error<256>( "FAIL: %s", message ) ;
        return value ;
    }

    bool_t run( void_t )
    {
        bool_t ok = true ;
        auto out = motor::shared( output_t( 3.0f ) ) ;
        auto a = motor::shared( input_t( -1.0f ) ) ;
        auto b = motor::shared( input_t( -2.0f ) ) ;

        // One output supplies independent inputs.
        {
            ok = check( a->connect( motor::share( out ) ), "connect first input" ) && ok ;
            ok = check( b->connect( motor::share( out ) ), "connect second input" ) && ok ;
            ok = check( a->get_value() == 3.0f && b->get_value() == 3.0f,
                "initial value on connect" ) && ok ;
            out->set_value( 7.0f ) ;
            ok = check( a->get_value() == 3.0f, "set alone does not push" ) && ok ;
            ok = check( out->exchange(), "exchange reports change" ) && ok ;
            ok = check( a->get_value() == 7.0f && b->get_value() == 7.0f,
                "fan-out reaches both inputs" ) && ok ;
            ok = check( !out->exchange(), "second exchange has no change" ) && ok ;
            a->get_value_and_reset() ;
            ok = check( !a->has_changed() && b->has_changed(),
                "input change flags are independent" ) && ok ;
        }

        // Disconnect and reconnect explicitly, without changing the other receiver.
        {
            a->disconnect() ;
            out->set_and_exchange( 9.0f ) ;
            ok = check( a->get_value() == 7.0f && b->get_value() == 9.0f,
                "disconnected input keeps its value" ) && ok ;
            ok = check( a->connect( motor::share( out ) ) && a->get_value() == 9.0f,
                "late connection receives current value" ) && ok ;
        }

        // A rejected type must not consume the caller's remaining reference.
        {
            auto wrong = motor::shared( motor::wire::input_slot<int_t>( 42 ) ) ;
            ok = check( !wrong->connect( motor::share( out ) ), "reject incompatible type" ) && ok ;
            ok = check( wrong->get_value() == 42, "rejected input unchanged" ) && ok ;
            motor::wire::input_slot<int_t>::release( motor::move( wrong ) ) ;
        }

        output_t::release( motor::move( out ) ) ;
        auto remaining = a->get_output_slot() ;
        ok = check( remaining == nullptr, "output release disconnects receivers" ) && ok ;
        motor::release( motor::move( remaining ) ) ;
        input_t::release( motor::move( a ) ) ;
        input_t::release( motor::move( b ) ) ;
        return ok ;
    }
}

int main( int argc, char ** argv )
{
    bool ok = false ;
    {
        if( argc == 1 ) ok = this_file::run() ;
        else if( argc == 2 && motor::string_t( argv[1] ) == "borrow" )
        {
            motor::wire::named_outputs_t sheet ;
            auto * first = sheet.borrow_or_add( "value", motor::shared( this_file::output_t( 1.0f ) ) ) ;
            auto * again = sheet.borrow_or_add( "value", motor::shared( this_file::output_t( 2.0f ) ) ) ;
            ok = this_file::check( first == again, "borrow_or_add returns existing slot" ) ;
            // Neither borrowed pointer owns a reference. Sheet destruction must free it.
        }
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
