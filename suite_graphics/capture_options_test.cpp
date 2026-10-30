#include "capture_options.hpp"

#include <motor/log/global.h>

using namespace motor::core::types ;

int main( void_t )
{
    bool_t passed = true ;
    auto const check = [&]( char const * message, bool_t const ok )
    {
        motor::log::global_t::test( motor::string_t( "[capture] " ) + message, ok ) ;
        passed = passed && ok ;
    } ;

    {
        sample_support::capture_options options ;
        motor::application::window_info_t info ;
        info.x = 40 ; info.y = 60 ; info.w = 1280 ; info.h = 800 ;
        info.gen = motor::application::graphics_generation::gen4_d3d11 ;
        options.apply( info ) ;
        check( "default preserves dimensions and borders", info.w == 1280 && info.h == 800 && !info.borderless ) ;

        check( "square is accepted", options.parse( "square" ) ) ;
        options.apply( info ) ;
        check( "square requests a borderless 1080 x 1080 area", info.w == 1080 && info.h == 1080 && info.borderless ) ;

        check( "vertical is accepted", options.parse( "vertical" ) ) ;
        options.apply( info ) ;
        check( "vertical requests a borderless 9:16 area", info.w == 1080 && info.h == 1920 && info.borderless ) ;
        check( "backend and placement are preserved", info.x == 40 && info.y == 60 &&
            info.gen == motor::application::graphics_generation::gen4_d3d11 ) ;

        check( "unknown and empty formats are rejected", !options.parse( "landscape" ) && !options.parse( "" ) ) ;
        int_t width = 1920, height = 1080 ;
        options.apply( width, height ) ;
        check( "invalid input preserves the last valid format", width == 1080 && height == 1920 ) ;
        check( "offscreen and window dimensions agree", width == info.w && height == info.h ) ;

        sample_support::capture_options const copy = options ;
        info.w = 640 ; info.h = 480 ;
        copy.apply( info ) ;
        check( "copied options also apply when reopening a window", info.w == 1080 && info.h == 1920 ) ;
    }

    motor::log::global_t::deinit() ;
    return passed ? 0 : 1 ;
}
