#include <motor/concurrent/typedefs.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <atomic>

namespace this_file
{
    using namespace motor::core::types ;

    struct tracked
    {
        inline static std::atomic_size_t destroyed { 0 } ;
        size_t value = 42 ;
        ~tracked( void_t ) noexcept { destroyed.fetch_add( 1 ) ; }
    } ;

    bool_t sharing( void_t )
    {
        auto owner = motor::memory::create_ptr<tracked>() ;
        auto * borrowed = owner.mtr() ;
        auto shared = motor::share( borrowed ) ;
        auto moved = motor::move( owner ) ;
        bool_t ok = owner == nullptr && moved.mtr() == borrowed && shared.mtr() == borrowed ;
        motor::release( motor::move( moved ) ) ;
        ok = (tracked::destroyed.load() == 0 && shared->value == 42) && ok ;
        motor::release( motor::move( shared ) ) ;
        return ok && tracked::destroyed.load() == 1 ;
    }

    bool_t guard( void_t )
    {
        {
            auto owner = motor::memory::create_ptr<tracked>() ;
            auto early_return = [&]( void_t )
            {
                motor::mtr_release_guard<tracked> guard( motor::move( owner ) ) ;
                return guard.is_valid() && guard->value == 42 ;
            } ;
            if( !early_return() || owner != nullptr || tracked::destroyed.load() != 1 ) return false ;
        }
        {
            motor::mtr_release_guard<tracked> guard( motor::memory::create_ptr<tracked>() ) ;
            auto * escaped = guard.move() ;
            bool_t const ok = !guard.is_valid() && escaped->value == 42 ;
            motor::release( motor::move( escaped ) ) ;
            if( !ok ) return false ;
        }
        return tracked::destroyed.load() == 2 ;
    }

    bool_t concurrent( void_t )
    {
        auto owner = motor::memory::create_ptr<tracked>() ;
        auto * borrowed = owner.mtr() ;
        std::atomic_bool ok { true } ;
        {
            motor::vector<motor::concurrent::thread_t> workers ;
            for( size_t i = 0; i < 6; ++i )
            {
                workers.emplace_back( [&, borrowed]( void_t )
                {
                    for( size_t n = 0; n < 1000; ++n )
                    {
                        auto share = motor::share( borrowed ) ;
                        if( share->value != 42 ) ok.store( false ) ;
                        motor::release( motor::move( share ) ) ;
                    }
                } ) ;
            }
            for( auto & worker : workers ) worker.join() ;
        }
        if( tracked::destroyed.load() != 0 ) ok.store( false ) ;
        motor::release( motor::move( owner ) ) ;
        return ok.load() && tracked::destroyed.load() == 1 ;
    }
}

int main( int argc, char ** argv )
{
    int result = 2 ;
    {
        motor::string_t const mode = argc == 2 ? argv[1] : "" ;
        bool ok = false ;
        if( mode == "sharing" ) ok = this_file::sharing() ;
        else if( mode == "guard" ) ok = this_file::guard() ;
        else if( mode == "concurrent" ) ok = this_file::concurrent() ;
        else
        {
            motor::log::global_t::error( "Usage: 00_memory_ownership sharing|guard|concurrent" ) ;
            motor::log::global_t::deinit() ;
            return result ;
        }
        result = ok ? 0 : 1 ;
        if( ok ) motor::log::global_t::status<256>( "[memory ownership] %s: PASS", mode.c_str() ) ;
        else motor::log::global_t::error<256>( "[memory ownership] %s: FAIL", mode.c_str() ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return clean ? result : 1 ;
}
