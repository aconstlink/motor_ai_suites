#include <motor/concurrent/global.h>
#include <motor/graphics/render_engine.hpp>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/vector>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace this_file
{
    using namespace motor::core::types ;
    using global_t = motor::concurrent::global_t ;
    using task_t = motor::concurrent::task_t ;

    bool_t run( void_t )
    {
        std::mutex mutex ;
        std::condition_variable cv ;
        bool_t pending = false, done = false ;
        std::atomic_size_t calls_a {0}, calls_b {0}, visits {0} ;
        std::atomic_bool ordered {true} ;
        auto const a = global_t::register_work_event_notify([&]( void_t ) { ++calls_a ; }) ;
        auto const b = global_t::register_work_event_notify([&]( void_t )
        {
            ++calls_b ;
            { std::lock_guard<std::mutex> lk(mutex) ; pending = true ; }
            cv.notify_one() ;
        }) ;
        global_t::unregister_work_event_notify(a) ;
        motor::vector<motor::concurrent::task_mtr_safe_t> nodes ;
        for( size_t i = 0 ; i < 8 ; ++i )
        {
            nodes.emplace_back(global_t::make_task([&,i]( task_t::task_funk_param_in_t )
            {
                // Let the consumer block before this task queues its successor.
                std::this_thread::sleep_for(std::chrono::milliseconds(20)) ;
                if( visits.fetch_add(1) != i ) ordered = false ;
                if( i == 7 )
                {
                    { std::lock_guard<std::mutex> lk(mutex) ; done = true ; }
                    cv.notify_one() ; // Completion only; never pumps a successor.
                }
            })) ;
            if( i != 0 ) nodes[i-1]->then(motor::share(nodes[i])) ;
        }
        global_t::schedule(motor::share(nodes[0]),motor::concurrent::schedule_type::loose) ;
        bool_t completed = true ;
        auto const deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5) ;
        for( ;; )
        {
            std::unique_lock<std::mutex> lk(mutex) ;
            if( !cv.wait_until(lk,deadline,[&]( void_t ){ return pending || done ; }) )
            { completed = false ; break ; }
            if( done ) break ;
            pending = false ;
            lk.unlock() ;
            global_t::update() ; // No timeout polling or unconditional updates.
        }
        global_t::unregister_work_event_notify(b) ;
        auto const before = calls_b.load() ;
        std::atomic_bool cleanup_done {false} ;
        auto cleanup_task = global_t::make_task([&]( task_t::task_funk_param_in_t )
        {
            { std::lock_guard<std::mutex> lk(mutex) ; cleanup_done = true ; }
            cv.notify_one() ;
        }) ;
        global_t::schedule(motor::share(cleanup_task),motor::concurrent::schedule_type::loose) ;
        global_t::update() ; // Explicit dispatch only for the no-listener cleanup check.
        {
            std::unique_lock<std::mutex> lk(mutex) ;
            cv.wait_for(lk,std::chrono::seconds(5),[&]( void_t ){ return cleanup_done.load() ; }) ;
        }
        global_t::deinit() ; // Join workers before graph teardown or captured locals expire.
        for( auto & node : nodes ) node->disconnect() ;
        for( auto & node : nodes ) motor::release(motor::move(node)) ;
        motor::release(motor::move(cleanup_task)) ;
        bool_t const graph_ok = completed && ordered.load() && visits == 8 && before >= 8 ;
        bool_t const unregister_ok = calls_a == 0 && calls_b == before && cleanup_done.load() ;
        motor::log::global_t::test("[notify] delayed successors progress using notifications only",graph_ok) ;
        motor::log::global_t::test("[notify] independent registrations and scheduling after unregister",unregister_ok) ;

        size_t notifications = 0 ;
        motor::graphics::render_engine_t original ;
        original.set_work_notify([&]( void_t ) { ++notifications ; }) ;
        motor::graphics::render_engine_t final(std::move(original)) ;
        final.leave_frame() ;
        bool_t const submitted = notifications == 1 ;
        final.execute_frame() ;
        bool_t const render_ok = submitted && notifications == 2 ;
        motor::log::global_t::test("[notify] render-engine callback survives move",render_ok) ;
        size_t submitted_notifications = 0, committed_notifications = 0 ;
        final.set_work_notify([&]( void_t )
        {
            if( final.can_execute() ) ++submitted_notifications ;
            else if( final.can_enter_frame() ) ++committed_notifications ;
        }) ;
        final.leave_frame() ; final.execute_frame() ;
        bool_t const ready = submitted_notifications == 1 && committed_notifications == 1 ;
        motor::log::global_t::test("[notify] submission and commit publish state before callback outside lock",ready) ;
        return graph_ok && unregister_ok && render_ok && ready ;
    }
}

int main( motor::core::types::void_t )
{
    bool const ok = this_file::run() ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return ok && clean ? 0 : 1 ;
}
