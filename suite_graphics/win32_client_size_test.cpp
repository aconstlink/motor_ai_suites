#include <motor/platform/global.h>
#include <motor/application/window/window_message_listener.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>

#include <windows.h>
#include <chrono>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;
    using listener_t = motor::application::window_message_listener_t ;
    using state_t = listener_t::state_vector_t ;
    using clock_t = std::chrono::steady_clock ;

    template< typename predicate_t >
    bool_t wait_for( listener_t * listener, predicate_t predicate )
    {
        auto const end = clock_t::now() + std::chrono::seconds( 3 ) ;
        do
        {
            state_t state ;
            if( listener->swap_and_reset( state ) && predicate( state ) ) return true ;
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) ) ;
        } while( clock_t::now() < end ) ;
        return false ;
    }

    bool_t run( bool_t const unaware )
    {
        using set_dpi_t = HANDLE ( WINAPI * )( HANDLE ) ;
        auto const set_dpi = reinterpret_cast< set_dpi_t >(
            GetProcAddress( GetModuleHandleW( L"user32.dll" ), "SetThreadDpiAwarenessContext" ) ) ;
        HANDLE const context = reinterpret_cast< HANDLE >( unaware ? INT_PTR( -1 ) : INT_PTR( -3 ) ) ;
        HANDLE const previous = set_dpi ? set_dpi( context ) : nullptr ;

        motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier() ;
        struct window_case
        {
            motor::string_t name ;
            motor::application::iwindow_mtr_t window = nullptr ;
            listener_t * listener = nullptr ;
            HWND hwnd = nullptr ;
            int_t width = 640, height = 360 ;
        } ;
        window_case cases[4] ;
        for( size_t i = 0 ; i < 4 ; ++i )
        {
            auto & item = cases[i] ;
            item.name = "motor_client_size_" + motor::to_string( GetCurrentProcessId() ) + "_" + motor::to_string( i ) ;
            item.listener = motor::memory::create_ptr< listener_t >( "[client size] listener" ) ;
            motor::application::window_info_t info ;
            if( i >= 2 ) { item.width = 1080 ; item.height = 1920 ; }
            info.window_name = item.name ; info.x = 40 ; info.y = 60 ; info.w = item.width ; info.h = item.height ;
            info.borderless = i % 2 == 1 ; info.show = false ;
            info.gen = motor::application::graphics_generation::none ;
            item.window = carrier->create_window( info ) ;
            item.window->register_out( motor::share( item.listener ) ) ;
        }

        bool_t passed = true ;
        std::thread worker( [&]( void_t )
        {
            HANDLE const old = set_dpi ? set_dpi( context ) : nullptr ;
            auto const check = [&]( motor::string_cref_t name, bool_t const ok )
            {
                motor::log::global_t::test( "[client size] " + name, ok ) ;
                passed = passed && ok ;
            } ;
            for( auto & item : cases )
            {
                auto const size_is = []( int_t const w, int_t const h )
                {
                    return [=]( state_t const & state )
                    { return state.resize_changed && state.resize_msg.w == size_t( w ) && state.resize_msg.h == size_t( h ) ; } ;
                } ;
                bool_t const created = wait_for( item.listener, size_is( item.width, item.height ) ) ;
                item.hwnd = FindWindowA( nullptr, item.name.c_str() ) ;
                check( "creation reports the requested client size", created && item.hwnd != nullptr ) ;
                if( !item.hwnd ) continue ;

                auto const verify = [&]( int_t const w, int_t const h )
                {
                    RECT client ;
                    bool_t const queried = GetClientRect( item.hwnd, &client ) != FALSE ;
                    if( queried && ( client.right - client.left != w || client.bottom - client.top != h ) )
                        motor::log::global_t::status<256>( "CLIENT_SIZE requested=%dx%d actual=%ldx%ld",
                            w, h, client.right - client.left, client.bottom - client.top ) ;
                    check( "GetClientRect matches " + motor::to_string( w ) + " x " + motor::to_string( h ),
                        queried && client.right - client.left == w && client.bottom - client.top == h ) ;
                    check( "window remains hidden", !IsWindowVisible( item.hwnd ) ) ;
                } ;
                verify( item.width, item.height ) ;
                RECT initial ; GetWindowRect( item.hwnd, &initial ) ;
                for( int_t const height : { 1080, 1920, 360 } )
                {
                    int_t const width = height == 360 ? 640 : 1080 ;
                    item.window->send_message( motor::application::resize_message_t{ false, 0, 0, true,
                        size_t( width ), size_t( height ) } ) ;
                    PostMessage( item.hwnd, WM_NULL, 0, 0 ) ;
                    check( "resize notification carries client dimensions", wait_for( item.listener, size_is( width, height ) ) ) ;
                    verify( width, height ) ;
                    RECT outer ; GetWindowRect( item.hwnd, &outer ) ;
                    check( "size-only resize preserves position", outer.left == initial.left && outer.top == initial.top ) ;
                }

                item.window->send_message( motor::application::resize_message_t{ true, 100, 120, false, 0, 0 } ) ;
                PostMessage( item.hwnd, WM_NULL, 0, 0 ) ;
                check( "position-only change is reported", wait_for( item.listener, []( state_t const & state )
                { return state.resize_changed && state.resize_msg.x == 100 && state.resize_msg.y == 120 ; } ) ) ;
                verify( 640, 360 ) ;

                item.window->send_message( motor::application::resize_message_t{ true, 80, 90, true, 800, 600 } ) ;
                PostMessage( item.hwnd, WM_NULL, 0, 0 ) ;
                check( "combined move and resize is reported", wait_for( item.listener, []( state_t const & state )
                { return state.resize_changed && state.resize_msg.x == 80 && state.resize_msg.y == 90 &&
                    state.resize_msg.w == 800 && state.resize_msg.h == 600 ; } ) ) ;
                verify( 800, 600 ) ;

                for( auto const borderless : { motor::application::three_state::on, motor::application::three_state::off } )
                {
                    item.window->send_message( motor::application::show_message_t{ false, borderless } ) ;
                    PostMessage( item.hwnd, WM_NULL, 0, 0 ) ;
                    check( "style change preserves client dimensions", wait_for( item.listener, size_is( 800, 600 ) ) ) ;
                    verify( 800, 600 ) ;
                }
                PostMessage( item.hwnd, WM_CLOSE, 0, 0 ) ;
                check( "hidden window closes", wait_for( item.listener, []( state_t const & state )
                { return state.close_changed ; } ) ) ;
            }
            carrier->close() ;
            if( set_dpi ) set_dpi( old ) ;
        } ) ;

        auto const result = carrier->on_exec() ;
        worker.join() ;
        for( auto & item : cases )
        {
            motor::release( motor::move( item.window ) ) ;
            motor::release( motor::move( item.listener ) ) ;
        }
        motor::concurrent::global_t::deinit() ;
        motor::release( motor::move( carrier ) ) ;
        if( set_dpi ) set_dpi( previous ) ;
        return passed && result == motor::application::result::ok ;
    }
}

int main( int argc, char ** argv )
{
    bool const unaware = argc == 2 && motor::string_t( argv[1] ) == "--dpi-unaware" ;
    bool const passed = this_file::run( unaware ) ;
    motor::log::global_t::deinit() ;
    return passed ? 0 : 1 ;
}
