#include "ring_filters.hpp"
#include <motor/log/global.h>
#include <motor/concurrent/global.h>

int main()
{
    bool passed = true ;
    {
        ring_sample::filters rings ;
        auto check = [&](char const * name, bool ok)
        {
            motor::log::global_t::test(name,ok) ;
            passed &= ok ;
        } ;
        auto near = [](float a, float b){ return std::abs(a-b) < 0.001f ; } ;
        check("filter connections",rings.valid()) ;
        rings.step(10,0.75f,false) ;
        check("idle before first kick",rings.tilt()==0 && rings.glow()==0 && rings.latched_tilt()==0) ;
        rings.step(10,0.75f,true) ;
        check("kick reaches three receivers",near(rings.tilt(),20) && rings.glow()==1 && near(rings.latched_tilt(),30)) ;
        rings.step(10,0.1f,false) ;
        check("paused clock preserves pose and samples strength only at onset",
            near(rings.tilt(),20) && rings.glow()==1 && near(rings.latched_tilt(),30)) ;
        rings.step(10.5f,0.1f,false) ;
        check("gate resets at exactly half a second",rings.glow()==0) ;
        rings.step(11,0.1f,false) ;
        check("decay at midpoint; strength still latched",near(rings.tilt(),3.64851f) && near(rings.latched_tilt(),30)) ;
        rings.step(12,0.5f,true) ;
        check("new kick accepted exactly at old endpoint",
            near(rings.tilt(),20) && rings.glow()==1 && near(rings.latched_tilt(),20)) ;
        rings.step(15,1,false) ;
        check("skipping past endpoint resets every receiver",rings.tilt()==0 && rings.glow()==0 && rings.latched_tilt()==0) ;
    }
    motor::concurrent::global_t::deinit() ;
    motor::log::global_t::deinit() ;
    return motor::memory::global_t::dump_to_std() != 0 || !passed ? 1 : 0 ;
}
