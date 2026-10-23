#include <motor/graphics/render_engine.hpp>
#include <motor/graphics/backend/gen4/backend.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <memory>

#if defined( _MSC_VER )
#define COMMAND_NOINLINE __declspec(noinline)
#else
#define COMMAND_NOINLINE __attribute__((noinline))
#endif

namespace this_file
{
    using namespace motor::core::types;
    using command_t = motor::graphics::render_engine::command_t;
    using clock_t = std::chrono::steady_clock;
    using detail_t = motor::graphics::gen4::backend::render_detail_t;
    enum class strategy { clear, index_keep, index_release };
    constexpr std::array<char const *, 3> strategy_names{ "clear", "index_keep", "index_release" };
    constexpr std::array<char const *, 3> capture_names{ "small", "draw_like", "large" };
    struct accumulator { uint64_t sum = 0; size_t calls = 0; };
    struct object_data { uint64_t token = 1; };

    template < size_t Capture >
    COMMAND_NOINLINE command_t make_command( accumulator * target, object_data const * object,
        uint64_t const value )
    {
        command_t command;
        if constexpr( Capture == 0 )
        {
            command.exec_funk = [ target, value ]( void_t )
            { target->sum += value + 1; ++target->calls; };
        }
        else if constexpr( Capture == 1 )
        {
            detail_t detail;
            detail.varset = size_t( value );
            // Same capture shape as frontend render: two pointers and render_detail.
            command.exec_funk = [ target, object, detail ]( void_t )
            { target->sum += uint64_t( detail.varset ) + object->token; ++target->calls; };
        }
        else
        {
            std::array<uint64_t, 32> payload{};
            payload.front() = value;
            payload.back() = 1;
            command.exec_funk = [ target, payload ]( void_t )
            {
                for( auto const value : payload ) target->sum += value;
                ++target->calls;
            };
        }
        return command;
    }

    struct queue
    {
        motor::vector<command_t> slots;
        size_t active = 0;
        strategy mode;

        queue( strategy const value, size_t const capacity ) : mode( value )
        {
            slots.reserve( capacity );
            if( mode != strategy::clear ) slots.resize( capacity );
        }
        template < size_t Capture >
        COMMAND_NOINLINE void_t fill( size_t const count, uint64_t const seed,
            accumulator & target, object_data const & object )
        {
            for( size_t i = 0; i < count; ++i )
            {
                auto command = make_command<Capture>( &target, &object, seed + i );
                if( mode == strategy::clear ) slots.emplace_back( std::move( command ) );
                else slots[i] = std::move( command );
            }
            active = count;
        }
        COMMAND_NOINLINE void_t execute( void_t )
        {
            for( size_t i = 0; i < active; ++i ) slots[i].exec_funk();
        }
        COMMAND_NOINLINE void_t reset( void_t )
        {
            if( mode == strategy::clear ) slots.clear();
            else if( mode == strategy::index_release )
            {
                for( size_t i = 0; i < active; ++i ) slots[i] = command_t{};
            }
            active = 0;
        }
    };

    struct sample
    {
        double fill = 0, execute = 0, reset = 0, total = 0;
    };
    double milliseconds( clock_t::time_point a, clock_t::time_point b )
    { return std::chrono::duration<double, std::milli>( b - a ).count(); }
    bool_t matches( accumulator const & value, size_t const count, uint64_t const seed )
    {
        return value.calls == count && value.sum == uint64_t( count ) * ( seed + 1 ) +
            uint64_t( count ) * uint64_t( count == 0 ? 0 : count - 1 ) / 2;
    }

    template < size_t Capture >
    bool_t verify_capture( void_t )
    {
        for( size_t s = 0; s < strategy_names.size(); ++s )
        {
            queue q( strategy( s ), 8 );
            object_data object;
            accumulator target;
            uint64_t seed = 10;
            for( size_t const count : { size_t( 8 ), size_t( 2 ), size_t( 0 ), size_t( 5 ), size_t( 1 ) } )
            {
                target = {};
                q.fill<Capture>( count, seed, target, object );
                q.execute();
                if( !matches( target, count, seed ) ) return false;
                q.reset();
                q.execute();
                if( !matches( target, count, seed ) ) return false;
                seed += 100;
            }
        }
        return true;
    }

    struct owned_capture
    {
        size_t * live;
        explicit owned_capture( size_t & count ) : live( &count ) { ++*live; }
        ~owned_capture( void_t ) { --*live; }
    };
    bool_t verify_lifetime( void_t )
    {
        for( size_t s = 0; s < strategy_names.size(); ++s )
        {
            size_t live = 0;
            {
                queue q( strategy( s ), 1 );
                command_t c;
                c.exec_funk = [ owned = std::make_shared<owned_capture>( live ) ]( void_t ) {};
                if( q.mode == strategy::clear ) q.slots.emplace_back( std::move( c ) );
                else q.slots[0] = std::move( c );
                q.active = 1;
                if( live != 1 ) return false;
                q.reset();
                if( live != ( q.mode == strategy::index_keep ? 1u : 0u ) ) return false;
            }
            if( live != 0 ) return false;
        }
        return true;
    }

    template < size_t Capture >
    bool_t measure_batch( queue & q, size_t const count, size_t const frames, uint64_t const seed,
        accumulator & target, object_data const & object, sample & result )
    {
        for( size_t frame = 0; frame < frames; ++frame )
        {
            target = {};
            uint64_t const frame_seed = seed + uint64_t( frame ) * 100003;
            auto const begin = clock_t::now();
            q.fill<Capture>( count, frame_seed, target, object );
            auto const filled = clock_t::now();
            q.execute();
            auto const executed = clock_t::now();
            q.reset();
            auto const reset = clock_t::now();
            if( !matches( target, count, frame_seed ) ) return false;
            result.fill += milliseconds( begin, filled );
            result.execute += milliseconds( filled, executed );
            result.reset += milliseconds( executed, reset );
            result.total += milliseconds( begin, reset );
        }
        result.fill /= double( frames ); result.execute /= double( frames );
        result.reset /= double( frames ); result.total /= double( frames );
        return true;
    }

    double median( motor::vector<double> values )
    {
        std::sort( values.begin(), values.end() );
        return ( values[(values.size()-1)/2] + values[values.size()/2] ) * 0.5;
    }
    template < size_t Capture >
    bool_t benchmark_capture( size_t const count, std::ofstream & csv )
    {
        constexpr size_t rounds = 15, frames = 8;
        object_data object;
        accumulator target;
        std::array<queue, 3> queues{ queue( strategy::clear, count ),
            queue( strategy::index_keep, count ), queue( strategy::index_release, count ) };
        std::array<motor::vector<sample>, 3> samples;
        for( auto & values : samples ) values.reserve( rounds );
        for( auto & q : queues )
        {
            sample warmup;
            if( !measure_batch<Capture>( q, count, 3, 0, target, object, warmup ) ) return false;
        }
        for( size_t round = 0; round < rounds; ++round )
        {
            for( size_t offset = 0; offset < queues.size(); ++offset )
            {
                size_t const s = ( round + offset ) % queues.size();
                sample value;
                if( !measure_batch<Capture>( queues[s], count, frames, 1000003 * round,
                    target, object, value ) ) return false;
                samples[s].emplace_back( value );
                if( csv.is_open() ) csv << count << ',' << capture_names[Capture] << ',' << strategy_names[s]
                    << ',' << round << ',' << frames << ',' << value.fill << ',' << value.execute
                    << ',' << value.reset << ',' << value.total << '\n';
            }
        }
        for( size_t s = 0; s < queues.size(); ++s )
        {
            motor::vector<double> fills, executions, resets, totals, management;
            for( auto const & value : samples[s] )
            {
                fills.emplace_back( value.fill ); executions.emplace_back( value.execute );
                resets.emplace_back( value.reset ); totals.emplace_back( value.total );
                management.emplace_back( value.fill + value.reset );
            }
            // index_keep still owns its final frame's captures. Report that cleanup separately.
            auto const begin = clock_t::now();
            queues[s].slots.clear();
            auto const end = clock_t::now();
            motor::log::global_t::status<512>(
                "COMMAND,%zu,%s,%s,fill=%.6f,execute=%.6f,reset=%.6f,fill_reset=%.6f,total=%.6f,final_cleanup=%.6f ms",
                count, capture_names[Capture], strategy_names[s], median( fills ), median( executions ),
                median( resets ), median( management ), median( totals ), milliseconds( begin, end ) );
        }
        return true;
    }
}

int main( int argc, char ** argv )
{
    bool ok = true;
    {
        bool verify_only = false;
        motor::string_t path;
        for( int i = 1; i < argc; ++i )
        {
            motor::string_t const arg = argv[i];
            if( arg == "--verify-only" ) verify_only = true;
            else if( arg == "--csv" && i + 1 < argc ) path = argv[++i];
            else ok = false;
        }
        if( !ok ) motor::log::global_t::status( "Usage: 04_command_storage [--verify-only] [--csv file]" );
        bool const captures = this_file::verify_capture<0>() && this_file::verify_capture<1>() &&
            this_file::verify_capture<2>();
        bool const lifetime = this_file::verify_lifetime();
        motor::log::global_t::test( "[commands] checksums, shrinking/empty frames, active range", captures );
        motor::log::global_t::test( "[commands] retained captures and eventual destruction", lifetime );
        ok = ok && captures && lifetime;
        if( ok && !verify_only )
        {
            std::ofstream csv;
            if( !path.empty() )
            {
                csv.open( path.c_str() );
                ok = csv.is_open();
                csv << std::fixed << std::setprecision(9)
                    << "commands,capture,strategy,round,frames,fill_ms,execute_ms,reset_ms,total_ms\n";
            }
#if !defined( NDEBUG )
            motor::log::global_t::warning( "Use an optimized build for performance comparisons." );
#endif
            motor::log::global_t::status<256>( "sizeof(command)=%zu sizeof(std::function)=%zu sizeof(render_detail)=%zu",
                sizeof(this_file::command_t), sizeof(this_file::command_t::funk_t), sizeof(this_file::detail_t) );
            for( size_t const count : { size_t( 1000 ), size_t( 10000 ) } )
            {
                if( !ok ) break;
                ok = this_file::benchmark_capture<0>( count, csv ) &&
                    this_file::benchmark_capture<1>( count, csv ) && this_file::benchmark_capture<2>( count, csv );
            }
            if( csv.is_open() ) { csv.flush(); ok = ok && csv.good(); }
            motor::log::global_t::test( "[commands] timed checksums and CSV output", ok );
        }
    }
    motor::log::global_t::deinit();
    bool const clean = motor::memory::global_t::dump_to_std() == 0;
    return ok && clean ? 0 : 1;
}
