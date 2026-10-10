#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/memory/malloc_guard.hpp>
#include <motor/std/string>
#include <motor/std/vector>

#include <array>
#include <cstring>
#include <utility>

namespace this_file
{
    using namespace motor::core::types ;
    using memory_t = motor::memory::global_t ;

    bool_t raw( void_t )
    {
        auto const before = memory_t::get_sib() ;
        bool_t ok = true ;
        bool_t purposes_ok = true ;
        for( size_t const size : {size_t(1), size_t(7), size_t(64), size_t(4097)} )
        {
            auto * bytes = memory_t::alloc_raw<unsigned char>( size, "memory raw test" ) ;
            char const * purpose = nullptr ;
            purposes_ok = memory_t::get_purpose( bytes, purpose ) && purposes_ok ;
            purposes_ok = (purpose != nullptr && std::strcmp( purpose, "memory raw test" ) == 0) && purposes_ok ;
            ok = (memory_t::get_sib() == before + size) && ok ;
            for( size_t i = 0; i < size; ++i ) bytes[i] = static_cast<unsigned char>(i % 251) ;
            for( size_t i = 0; i < size; ++i ) ok = (bytes[i] == i % 251) && ok ;
            bytes = memory_t::dealloc_raw( bytes ) ;
            ok = (bytes == nullptr && memory_t::get_sib() == before) && ok ;
        }
        if( !purposes_ok ) motor::log::global_t::error( "[memory] get_purpose did not return the allocation's purpose" ) ;
        if( !ok ) motor::log::global_t::error( "[memory] raw allocation contents or byte accounting mismatch" ) ;
        return ok && purposes_ok ;
    }

    struct element
    {
        inline static std::array<void const *, 4> addresses {} ;
        inline static std::array<size_t, 4> destructions {} ;
        ~element( void_t ) noexcept
        {
            for( size_t i = 0; i < addresses.size(); ++i )
                if( addresses[i] == this ) ++destructions[i] ;
        }
    } ;

    bool_t typed_array( void_t )
    {
        auto * data = memory_t::alloc<element>( size_t(4), "typed array test" ) ;
        for( size_t i = 0; i < 4; ++i ) element::addresses[i] = data + i ;
        data = memory_t::dealloc( data, size_t(4) ) ;
        bool_t ok = data == nullptr ;
        for( size_t i = 0; i < 4; ++i )
        {
            if( element::destructions[i] != 1 )
            {
                motor::log::global_t::error<256>( "[memory] element %zu destroyed %zu times; expected 1",
                    i, element::destructions[i] ) ;
                ok = false ;
            }
        }
        return ok ;
    }

    bool_t containers( void_t )
    {
        auto const before = memory_t::get_sib() ;
        bool_t ok = true ;
        {
            motor::vector<motor::string_t> values ;
            for( size_t i = 0; i < 100; ++i ) values.emplace_back( 128 + i, char('a' + i % 26) ) ;
            auto copy = values ;
            values[0][0] = 'z' ;
            auto moved = std::move( copy ) ;
            ok = moved.size() == 100 && moved[0][0] == 'a' ;
            for( size_t i = 0; i < moved.size(); ++i )
                ok = (moved[i] == motor::string_t(128 + i, char('a' + i % 26))) && ok ;
            ok = (memory_t::get_sib() > before) && ok ;
        }
        return ok && memory_t::get_sib() == before ;
    }

    bool_t malloc_copy( void_t )
    {
        auto const before = memory_t::get_sib() ;
        bool_t ok = true ;
        {
            unsigned int const source[] = {3, 7, 11, 19} ;
            motor::memory::malloc_guard<unsigned int> guard( source, 4 ) ;
            ok = guard.size() == 4 && guard.get() != source ;
            for( size_t i = 0; i < 4; ++i ) ok = guard.equals( i, source[i] ) && ok ;
            auto const allocated = memory_t::get_sib() - before ;
            ok = (allocated == sizeof(source)) && ok ;
            if( allocated != sizeof(source) ) motor::log::global_t::error<256>(
                "[memory] malloc_guard allocated %zu bytes; expected %zu", allocated, sizeof(source) ) ;
            if( !ok ) motor::log::global_t::error( "[memory] malloc_guard copy content or allocation size mismatch" ) ;
        }
        return ok && memory_t::get_sib() == before ;
    }

    bool_t malloc_move( bool_t const assignment )
    {
        auto const before = memory_t::get_sib() ;
        bool_t ok = true ;
        {
            motor::memory::malloc_guard<unsigned char> source( 19 ) ;
            source[0] = 42 ;
            auto * address = source.get() ;
            if( assignment )
            {
                motor::memory::malloc_guard<unsigned char> destination( 7 ) ;
                destination = std::move( source ) ;
                ok = source.get() == nullptr && destination.get() == address && destination[0] == 42 ;
            }
            else
            {
                motor::memory::malloc_guard<unsigned char> destination( std::move(source) ) ;
                ok = source.get() == nullptr && destination.get() == address && destination.size() == 19 ;
                auto * escaped = destination.move_ptr() ;
                ok = (destination.get() == nullptr && destination.size() == 0 && escaped[0] == 42) && ok ;
                memory_t::dealloc_raw( escaped ) ;
            }
        }
        return ok && memory_t::get_sib() == before ;
    }
}

int main( int argc, char ** argv )
{
    int result = 2 ;
    {
        motor::string_t const mode = argc == 2 ? argv[1] : "" ;
        motor::log::global_t::status<256>( "[memory allocations] %s", mode.c_str() ) ;
        bool ok = false ;
        if( mode == "raw" ) ok = this_file::raw() ;
        else if( mode == "typed_array" ) ok = this_file::typed_array() ;
        else if( mode == "containers" ) ok = this_file::containers() ;
        else if( mode == "malloc_copy" ) ok = this_file::malloc_copy() ;
        else if( mode == "malloc_move" ) ok = this_file::malloc_move( false ) ;
        else if( mode == "malloc_assignment" ) ok = this_file::malloc_move( true ) ;
        else
        {
            motor::log::global_t::error( "Usage: 01_memory_allocations raw|typed_array|containers|malloc_copy|malloc_move|malloc_assignment" ) ;
            motor::log::global_t::deinit() ;
            return result ;
        }
        result = ok ? 0 : 1 ;
        if( ok ) motor::log::global_t::status( "[memory allocations] PASS" ) ;
        else motor::log::global_t::error( "[memory allocations] FAIL" ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
