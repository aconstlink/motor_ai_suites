#include <motor/platform/global.h>
#include <motor/application/carrier.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;
    using task_t = motor::concurrent::task_t ;
    using global_t = motor::concurrent::global_t ;

    bool_t run( void_t )
    {
        motor::application::carrier_mtr_t carrier = motor::platform::global_t::create_carrier() ;
        std::mutex mutex ; std::condition_variable cv ;
        bool_t started = false, completed = false, passed = false ;
        clock_t::time_point submitted, closing ;
        double wake_ms = 0 ;
        auto first = global_t::make_task([&]( task_t::task_funk_param_in_t )
        {
            { std::lock_guard<std::mutex> lk(mutex) ; started = true ; }
            cv.notify_one() ;
        }) ;
        auto probe = global_t::make_task([&]( task_t::task_funk_param_in_t )
        {
            {
                std::lock_guard<std::mutex> lk(mutex) ;
                wake_ms = std::chrono::duration<double,std::milli>(clock_t::now()-submitted).count() ;
                completed = true ;
            }
            cv.notify_one() ;
        }) ;
        global_t::schedule(motor::share(first),motor::concurrent::schedule_type::loose) ;
        std::thread producer([&]( void_t )
        {
            std::unique_lock<std::mutex> lk(mutex) ;
            bool_t const ready = cv.wait_for(lk,std::chrono::seconds(5),[&]( void_t ){ return started ; }) ;
            lk.unlock() ;
            if( ready )
            {
                // No app update thread, windows, rendered frames or polling can wake the carrier here.
                std::this_thread::sleep_for(std::chrono::milliseconds(500)) ;
                { std::lock_guard<std::mutex> guard(mutex) ; submitted = clock_t::now() ; }
                global_t::schedule(motor::share(probe),motor::concurrent::schedule_type::loose) ;
                lk.lock() ;
                passed = cv.wait_for(lk,std::chrono::seconds(5),[&]( void_t ){ return completed ; }) ;
                lk.unlock() ;
                std::this_thread::sleep_for(std::chrono::milliseconds(100)) ;
            }
            closing = clock_t::now() ;
            carrier->close() ;
        }) ;
        // Exercise the real platform wait loop, intentionally without carrier::exec's app thread.
        auto const result = carrier->on_exec() ;
        auto const returned = clock_t::now() ;
        producer.join() ;
        global_t::deinit() ;
        motor::release(motor::move(first)) ;
        motor::release(motor::move(probe)) ;
        motor::release(motor::move(carrier)) ;
        bool_t const ok = passed && result == motor::application::result::ok ;
        motor::log::global_t::test("[carrier wakeup] delayed task wakes idle carrier, close returns",ok) ;
        motor::log::global_t::status<256>("CARRIER_WAKEUP task_ms=%.4f close_ms=%.4f",
            wake_ms,std::chrono::duration<double,std::milli>(returned-closing).count()) ;
        return ok ;
    }
}

int main( motor::core::types::void_t )
{
    bool const ok = this_file::run() ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
