#pragma once

#include <motor/application/carrier.h>
#include <motor/log/global.h>
#include <motor/std/vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <thread>

namespace village_diagnostics
{
    // Calibration on the app thread after frame/CPU measurement, not an engine-loop trace.
    inline bool sleep_probe( motor::string_cref_t frame_csv )
    {
        using namespace motor::core::types ;
        constexpr size_t count = 128 ;
        auto const requested = motor::application::carrier::get_cpu_sleep() ;
        motor::vector<double> samples ; samples.reserve(count) ;
        for( size_t i = 0 ; i < count ; ++i )
        {
            auto const start = std::chrono::steady_clock::now() ;
            if( requested > std::chrono::microseconds(0) ) std::this_thread::sleep_for(requested) ;
            samples.emplace_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()) ;
        }
        if( !frame_csv.empty() )
        {
            auto const path = frame_csv + ".sleep.csv" ;
            std::ofstream out(path.c_str()) ; out.imbue(std::locale::classic()) ;
            out << "sample,requested_us,elapsed_us\n" << std::fixed << std::setprecision(4) ;
            for( size_t i = 0 ; i < count ; ++i ) out << i << ',' << requested.count() << ',' << samples[i] << '\n' ;
            out.flush() ;
            if( !out.good() ) return false ;
        }
        double total = 0 ; for( auto const value : samples ) total += value ;
        std::sort(samples.begin(),samples.end()) ;
        motor::log::global_t::status<512>(
            "SLEEP_PROBE requested_us=%lld samples=%zu mean_us=%.4f median_us=%.4f p95_us=%.4f p99_us=%.4f max_us=%.4f",
            static_cast<long long>(requested.count()),count,total/double(count),
            (samples[count/2-1]+samples[count/2])*0.5,
            samples[size_t(std::ceil(.95*double(count)))-1],samples[size_t(std::ceil(.99*double(count)))-1],samples.back()) ;
        return true ;
    }
}
