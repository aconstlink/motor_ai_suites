#pragma once

#include <motor/application/window/window_info.h>
#include <motor/base/types.hpp>
#include <motor/std/string>

namespace sample_support
{
    using namespace motor::core::types ;

    struct capture_options
    {
        enum class format { original, square, vertical } ;
        format output = format::original ;

        bool_t parse( motor::string_cref_t value ) noexcept
        {
            if( value == "square" ) output = format::square ;
            else if( value == "vertical" ) output = format::vertical ;
            else return false ;
            return true ;
        }

        void_t apply( int_t & width, int_t & height ) const noexcept
        {
            if( output == format::original ) return ;
            width = 1080 ;
            height = output == format::square ? 1080 : 1920 ;
        }

        void_t apply( motor::application::window_info_t & info ) const noexcept
        {
            if( output == format::original ) return ;
            apply( info.w, info.h ) ;
            // Keep the capture area free of title bars and window borders.
            info.borderless = true ;
        }
    } ;
}
