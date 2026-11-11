#pragma once

#include <motor/wire/kit/trigger_filter_node.hpp>
#include <cmath>

namespace ring_sample
{
    class filters
    {
        using node_t = motor::wire::trigger_filter_node_t ;
        using input = node_t::signal_is_t ;
        using output = node_t::signal_os_t ;
        output * _clock = motor::shared(output(0)) ;
        output * _strength = motor::shared(output(0)) ;
        input * _receivers[3] = { motor::shared(input(0)), motor::shared(input(0)), motor::shared(input(0)) } ;
        node_t _node ;
        bool _valid = true ;

    public:
        filters()
        {
            _valid = _node.conenct_time_is(motor::share(_clock)) ;
            auto bind = [&](size_t index, float duration, node_t::filter_kernel_t curve)
            {
                auto id = _node.add_filter(std::move(curve)) ;
                _valid &= _node.set_duration(id,duration) ;
                _valid &= _node.reset_on_detrigger(id,true) ;
                _valid &= _node.connect_signal_is(id,motor::share(_strength)) ;
                _valid &= _node.connect_signal_os(id,motor::share(_receivers[index])) ;
            } ;
            bind(0,2.0f,[](auto const & d)
            {
                auto const end = std::exp(-3.0f) ;
                return 20.0f * (std::exp(-3.0f*d.time_rel/d.duration)-end)/(1.0f-end) ;
            }) ;
            bind(1,0.5f,[](auto const &){ return 1.0f ; }) ;
            bind(2,2.0f,[](auto const & d){ return 40.0f*d.sig_start ; }) ;
        }
        filters(filters const &) = delete ;
        filters & operator=(filters const &) = delete ;
        ~filters()
        {
            for(auto & slot : _receivers) input::release(motor::move(slot)) ;
            output::release(motor::move(_clock)) ;
            output::release(motor::move(_strength)) ;
        }
        bool valid() const { return _valid ; }
        void step(float seconds, float strength, bool kick)
        {
            _clock->set_and_exchange(seconds) ;
            _strength->set_and_exchange(strength) ;
            if(kick) _node.trigger() ;
            // Evaluate on the caller's thread before sampling the connected receivers.
            _node.execute() ;
        }
        float tilt() const { return _receivers[0]->get_value() ; }
        float glow() const { return _receivers[1]->get_value() ; }
        float latched_tilt() const { return _receivers[2]->get_value() ; }
    } ;
}
