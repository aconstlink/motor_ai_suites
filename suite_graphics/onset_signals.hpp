#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <motor/audio/util/frequency_bands.hpp>
#include <motor/audio/util/onset_detector.hpp>
#include <motor/log/global.h>
#include <motor/std/vector>

namespace onset_sample
{
    using namespace motor::core::types;

    struct band
    {
        std::optional< motor::audio::onset_detector > detector { std::in_place };
        float_t raw = 0.0f, pulse = 0.0f, decay = 0.25f;
        size_t count = 0;

        void_t reset( void_t ) noexcept
        {
            auto const parameters = detector->parameters();
            detector.emplace( parameters );
            raw = pulse = 0.0f;
            count = 0;
        }

        void_t update( float_t const input, float_t const dt ) noexcept
        {
            if( !std::isfinite( dt ) || dt <= 0.0f ) return;
            raw = std::isfinite( input ) ? std::max( input, 0.0f ) : 0.0f;
            pulse *= std::exp( -dt / decay );
            if( detector->update( raw, dt ) ) { pulse = 1.0f; ++count; }
        }
    };

    inline std::array< float_t, 3 > sum_bands( motor::vector< float_t > const & spectrum,
        float_t const bin_hz ) noexcept
    {
        std::array< float_t, 3 > result {};
        if( spectrum.empty() || !std::isfinite( bin_hz ) || bin_hz <= 0.0f ) return result;
        float_t const edges[] = { 40.0f, 250.0f, 2000.0f, 12000.0f };
        motor::audio::frequency_bands bands( bin_hz, spectrum.size() );
        for( size_t b = 0; b < result.size(); ++b )
        {
            bands.for_each( edges[b], edges[b+1], [&]( size_t const, size_t const bin )
            {
                float_t const value = spectrum[bin];
                if( std::isfinite( value ) && value > 0.0f ) result[b] += value;
            } );
        }
        return result;
    }

    // Known squared-amplitude inputs, not an FFT or generated audible music.
    inline std::array< float_t, 3 > synthetic_levels( double_t const seconds ) noexcept
    {
        std::array< float_t, 3 > result {};
        double_t const periods[] = { 0.5, 1.0, 0.25 };
        double_t const offsets[] = { 0.0, 0.25, 0.125 };
        for( size_t b = 0; b < result.size(); ++b )
        {
            if( seconds < offsets[b] ) continue;
            double_t const phase = std::fmod( seconds - offsets[b], periods[b] );
            result[b] = phase < 0.06 ? float_t( 0.012 * std::exp( -phase / 0.025 ) ) : 0.0f;
        }
        return result;
    }

    inline bool_t checks( void_t )
    {
        bool_t passed = true;
        auto check = [&]( char const * name, bool_t const value )
        { motor::log::global_t::test( name, value ); passed &= value; };
        {
            band b;
            for( size_t i = 0; i < 200; ++i ) b.update( 0.000001f, 0.005f );
            check( "Onset: sub-threshold noise stays quiet", b.count == 0 && b.pulse == 0.0f );
            b.update( 0.02f, 0.005f );
            check( "Onset: rising level triggers a unit visual pulse", b.count == 1 && b.pulse == 1.0f );
            for( size_t i = 0; i < 200; ++i ) b.update( 0.02f, 0.005f );
            check( "Onset: sustained level does not continuously retrigger", b.count == 1 && b.pulse < 0.03f );
            for( size_t i = 0; i < 200; ++i ) b.update( 0.0f, 0.005f );
            b.update( 0.02f, 0.005f );
            check( "Onset: silence rearms the next kick", b.count == 2 && b.pulse == 1.0f );
            auto const count = b.count;
            b.update( 1.0f, 0.0f );
            check( "Onset: zero dt leaves detector unchanged", b.count == count && b.pulse == 1.0f );
            b.reset();
            check( "Onset: reset clears history", b.count == 0 && b.detector->envelope_value() == 0.0f );
        }
        {
            motor::vector< float_t > bins( 1301, 0.0f );
            bins[4] = 1.0f; bins[24] = 2.0f; bins[25] = 4.0f;
            bins[199] = 8.0f; bins[200] = 16.0f; bins[1199] = 32.0f;
            bins[1200] = 64.0f;
            auto const sums = sum_bands( bins, 10.0f );
            check( "Bands: low/mid/high boundaries do not overlap", sums[0] == 3 && sums[1] == 12 && sums[2] == 48 );
            auto const invalid = sum_bands( bins, 0.0f );
            check( "Bands: missing sample rate is safe", invalid[0] == 0 && invalid[1] == 0 && invalid[2] == 0 );
        }
        {
            std::array< band, 3 > bands;
            for( size_t i = 0; i < 800; ++i )
            {
                auto const levels = synthetic_levels( double_t(i) * 0.005 );
                for( size_t b = 0; b < 3; ++b ) bands[b].update( levels[b], 0.005f );
            }
            check( "Synthetic: eight low, four mid and sixteen high onsets", bands[0].count == 8 && bands[1].count == 4 && bands[2].count == 16 );
            band a, b;
            a.pulse = b.pulse = 1.0f;
            for( size_t i = 0; i < 100; ++i ) a.update( 0.0f, 0.01f );
            for( size_t i = 0; i < 200; ++i ) b.update( 0.0f, 0.005f );
            check( "Visual decay is independent of update interval", std::abs(a.pulse-b.pulse) < 0.00001f );
        }
        return passed;
    }
}
