#pragma once

#include "scene_bounds.hpp"
#include <motor/log/global.h>
#include <chrono>
#include <numeric>

namespace culling_stress
{
    using namespace intersection_sample ;
    using clock_t = std::chrono::steady_clock ;
    inline double_t elapsed( clock_t::time_point const start ) noexcept
    { return std::chrono::duration<double_t, std::milli>( clock_t::now() - start ).count() ; }

    enum class mode { none, flat, hierarchy } ;
    inline constexpr char const * names[] = { "No culling", "Per-object AABB", "Hierarchical AABB" } ;
    inline constexpr size_t chunk_size = 64, sector_size = 16 ;
    inline constexpr size_t debug_group_limit = 256 ;
    inline constexpr size_t debug_leaf_limit = 128 ;

    inline size_t debug_bounds_count( size_t const total, size_t const limit, bool_t const all ) noexcept
    { return all ? total : std::min( limit, total ) ; }

    // Spread a limited selection over the full range; count == total selects every item.
    inline size_t debug_bounds_index( size_t const sample, size_t const total, size_t const count ) noexcept
    {
        if( count == total ) return sample ;
        return count <= 1 ? 0 : sample * ( total - 1 ) / ( count - 1 ) ;
    }

    struct counts
    {
        size_t group_tests = 0, leaf_tests = 0, rejected_groups = 0, accepted_groups = 0 ;
    } ;

    // Nodes own components/children; these records borrow them for the hot path.
    struct item
    {
        bounds_t * bounds = nullptr ;
        trafo_component_t * transform = nullptr ;
        vec3_t local, color ;
    } ;
    struct branch
    {
        group_t * node = nullptr ;
        size_t first = 0, end = 0 ;
        vec3_t offset = vec3_t( 0 ) ;
    } ;

    class field
    {
        group_t * _root = nullptr ;
        size_t _animated = 0 ;
    public:
        motor::vector<item> items ;
        motor::vector<branch> sectors, chunks ;
        motor::scene::trafo_visitor_t visitor ;

        field( void_t ) = default ;
        field( field const & ) = delete ;
        field & operator=( field const & ) = delete ;
        ~field( void_t ) { clear() ; }

        void_t clear( void_t )
        {
            items.clear() ; sectors.clear() ; chunks.clear() ;
            motor::release( motor::move( _root ) ) ; _root = nullptr ; _animated = 0 ;
        }
        aabb_t const & bounds( void_t ) const { return world_bounds( _root ) ; }

        void_t build( size_t const count )
        {
            clear() ; items.reserve( count ) ;
            _root = motor::shared( group_t() ) ;
            size_t const nc = ( count + chunk_size - 1 ) / chunk_size ;
            size_t const side = size_t( std::ceil( std::cbrt( double_t( nc ) ) ) ) ;
            chunks.reserve( nc ) ; sectors.reserve( ( nc + sector_size - 1 ) / sector_size ) ;
            for( size_t c = 0 ; c < nc ; ++c )
            {
                if( c % sector_size == 0 )
                {
                    auto sector = motor::shared( group_t() ) ;
                    sectors.emplace_back( branch{ sector, c, std::min( c + sector_size, nc ) } ) ;
                    _root->add_child( motor::move( sector ) ) ;
                }
                auto chunk = motor::shared( group_t() ) ;
                chunks.emplace_back( branch{ chunk, c * chunk_size, std::min( ( c + 1 ) * chunk_size, count ) } ) ;
                vec3_t const offset = vec3_t( float_t( c % side ), float_t( c / ( side * side ) ),
                    float_t( ( c / side ) % side ) ) * 10.0f - vec3_t( float_t( side - 1 ) * 5.0f ) ;
                chunks.back().offset = offset ;
                chunk->add_component( motor::shared( trafo_component_t() ) ) ;
                set_pose( chunks.back().node, pose( offset, 0 ) ) ;
                sectors.back().node->add_child( motor::move( chunk ) ) ;
                auto const & range = chunks.back() ;
                for( size_t i = range.first ; i < range.end ; ++i )
                {
                    size_t const k = i - range.first ;
                    vec3_t const p = vec3_t( float_t( k % 4 ), float_t( k / 16 ), float_t( ( k / 4 ) % 4 ) ) * 2.0f - vec3_t( 3 ) ;
                    auto leaf = motor::shared( leaf_t() ) ;
                    auto bounds = motor::shared( bounds_t( aabb_t( vec3_t( .55f ), vec3_t( -.55f ) ) ) ) ;
                    auto transform = motor::shared( trafo_component_t() ) ;
                    transform->set_trafo( pose( p, 0 ) ) ;
                    vec3_t const palette[] = { vec3_t( .12f, .68f, .65f ), vec3_t( .95f, .47f, .22f ),
                        vec3_t( .38f, .57f, .92f ), vec3_t( .83f, .74f, .36f ) } ;
                    items.emplace_back( item{ bounds, transform, p, palette[c % 4] } ) ;
                    leaf->add_component( motor::move( bounds ) ) ;
                    leaf->add_component( motor::move( transform ) ) ;
                    range.node->add_child( motor::move( leaf ) ) ;
                }
            }
            update() ;
        }
        void_t update( void_t ) { node_t::traverser( _root ).apply( &visitor ) ; }

        bool_t animate( float_t const time, size_t const percent )
        {
            size_t const active = items.size() * percent / 100 ;
            if( active == 0 && _animated == 0 ) return false ;
            for( size_t i = 0 ; i < std::max( active, _animated ) ; ++i )
            {
                auto p = items[i].local ;
                if( i < active ) p.y( p.y() + .65f * std::sin( time * .8f + float_t( i % 113 ) * .19f ) ) ;
                items[i].transform->set_trafo( pose( p, 0 ) ) ;
            }
            _animated = active ; update() ; return true ;
        }

        counts cull( frustum_t const & frustum, mode const method, motor::vector<size_t> & visible ) const
        {
            counts stats ; visible.clear() ;
            auto const append = [&]( size_t const first, size_t const end )
            { for( size_t i = first ; i < end ; ++i ) visible.emplace_back( i ) ; } ;
            auto const leaves = [&]( size_t const first, size_t const end )
            {
                for( size_t i = first ; i < end ; ++i )
                {
                    ++stats.leaf_tests ;
                    if( hit_t::hit_test_frustum_aabb( frustum, items[i].bounds->get_coarse_transformed() ) != result_t::outside )
                        visible.emplace_back( i ) ;
                }
            } ;
            auto const classify_group = [&]( node_t const * node )
            {
                ++stats.group_tests ;
                auto const result = hit_t::hit_test_frustum_aabb( frustum, world_bounds( node ) ) ;
                if( result == result_t::outside ) ++stats.rejected_groups ;
                else if( result == result_t::inside ) ++stats.accepted_groups ;
                return result ;
            } ;
            if( method == mode::none ) { append( 0, items.size() ) ; return stats ; }
            if( method == mode::flat ) { leaves( 0, items.size() ) ; return stats ; }
            auto const root = classify_group( _root ) ;
            if( root == result_t::outside ) return stats ;
            if( root == result_t::inside ) { append( 0, items.size() ) ; return stats ; }
            for( auto const & sector : sectors )
            {
                auto const sr = classify_group( sector.node ) ;
                if( sr == result_t::outside ) continue ;
                if( sr == result_t::inside )
                { append( chunks[sector.first].first, chunks[sector.end - 1].end ) ; continue ; }
                for( size_t c = sector.first ; c < sector.end ; ++c )
                {
                    auto const & chunk = chunks[c] ;
                    auto const cr = classify_group( chunk.node ) ;
                    if( cr == result_t::outside ) continue ;
                    if( cr == result_t::inside ) append( chunk.first, chunk.end ) ;
                    else leaves( chunk.first, chunk.end ) ;
                }
            }
            return stats ;
        }
    } ;

    inline void_t camera_for( camera_t & camera, rectangle const & area, aabb_t const & box,
        float_t const time, float_t const far_plane, float_t const fov )
    {
        auto const center = box.get_center() ;
        auto const ext = box.get_extend() ;
        vec3_t const eye = center + vec3_t( std::sin( time * .17f ) * ext.x() * .75f,
            ext.y() * .35f, std::cos( time * .17f ) * ext.z() * .75f ) ;
        auto const target = center + vec3_t( std::sin( time * .31f + 1 ) * ext.x() * .5f, -ext.y() * .2f, 0 ) ;
        camera.make_perspective_fov( float_t( area.width ), float_t( area.height ), fov, .2f, far_plane ) ;
        camera.look_at( eye, vec3_t( 0, 1, 0 ), target ) ;
    }

    inline void_t observer_for( camera_t & camera, rectangle const & area, aabb_t const & box, frustum_t const & frustum )
    {
        float_t const radius = box.get_dims().length() + 200.0f ;
        camera.look_at( box.get_center() + vec3_t( .8f, .9f, 1.1f ) * radius,
            vec3_t( 0, 1, 0 ), box.get_center() ) ;
        float_t const aspect = float_t( area.width ) / float_t( area.height ) ;
        float_t half = 1 ;
        auto const include = [&]( vec3_t const & p )
        {
            auto const v = camera.mat_view() * vec4_t( p, 1 ) ;
            half = std::max( half, std::max( std::abs( v.x() ) / aspect, std::abs( v.y() ) ) ) ;
        } ;
        for( auto const & p : box.points() ) include( p ) ;
        corners_t corners ; frustum.points( corners ) ;
        for( auto const & p : corners ) include( p ) ;
        camera.make_orthographic( half * 2.15f * aspect, half * 2.15f, .1f, radius * 4 ) ;
        camera.look_at( box.get_center() + vec3_t( .8f, .9f, 1.1f ) * radius,
            vec3_t( 0, 1, 0 ), box.get_center() ) ;
    }

    inline bool_t checks( size_t const requested )
    {
        bool_t ok = true ;
        auto const check = [&]( char const * label, bool_t const value )
        { motor::log::global_t::test( motor::string_t( "[culling stress] " ) + label, value ) ; ok = ok && value ; } ;
        bool_t debug_selection = true ;
        for( size_t const total : { size_t( 0 ), size_t( 1 ), size_t( 64 ), size_t( 128 ), size_t( 129 ), size_t( 256 ), size_t( 257 ), size_t( 1563 ), size_t( 3125 ), size_t( 200000 ) } )
        for( size_t const limit : { debug_group_limit, debug_leaf_limit } )
        for( bool_t const all_bounds : { false, true } )
        {
            size_t const count = debug_bounds_count( total, limit, all_bounds ) ;
            debug_selection = debug_selection && count == ( all_bounds ? total : std::min( total, limit ) ) ;
            size_t previous = 0 ;
            for( size_t i = 0 ; i < count ; ++i )
            {
                size_t const index = debug_bounds_index( i, total, count ) ;
                debug_selection = debug_selection && index < total && ( i == 0 ? index == 0 : index > previous ) ;
                if( all_bounds || total <= limit ) debug_selection = debug_selection && index == i ;
                previous = index ;
            }
            if( count > 0 ) debug_selection = debug_selection && previous == total - 1 ;
        }
        check( "all/limited group and leaf selections cover their range without duplicates or invalid indices", debug_selection ) ;
        field data ; motor::vector<size_t> flat, hierarchical, all ;
        for( size_t const count : { size_t( 1 ), size_t( 65 ), size_t( 1025 ), requested } )
        {
            data.build( count ) ; flat.reserve( count ) ; hierarchical.reserve( count ) ; all.reserve( count ) ;
            bool_t positions = true ;
            for( auto const & chunk : data.chunks ) for( size_t i = chunk.first ; i < chunk.end ; ++i )
            {
                auto const & b = data.items[i].bounds->get_coarse_transformed() ;
                auto const expected = chunk.offset + data.items[i].local ;
                auto const actual = ( data.items[i].transform->get_computed() * vec4_t( 0, 0, 0, 1 ) ).xyz() ;
                positions = positions && ( b.get_center() - expected ).length2() < 1e-6f &&
                    ( actual - expected ).length2() < 1e-6f && ( b.get_extend() - vec3_t( .55f ) ).length2() < 1e-6f ;
            }
            check( "static cube transforms and bounds match independent grid positions", positions ) ;
            bool_t equal = true, containment = true ;
            for( size_t step = 0 ; step < 8 ; ++step )
            {
                data.animate( float_t( step ), step % 3 == 0 ? 0 : 25 ) ;
                camera_t camera ;
                camera_for( camera, rectangle{ 0, 0, step % 2 == 0 ? 1080u : 640u, 900 }, data.bounds(), float_t( step * 9 ), 45, 1.05f ) ;
                data.cull( camera.get_frustum(), mode::flat, flat ) ;
                data.cull( camera.get_frustum(), mode::hierarchy, hierarchical ) ;
                equal = equal && flat == hierarchical ;
                data.cull( camera.get_frustum(), mode::none, all ) ;
                equal = equal && all.size() == count ;
                for( size_t i = 0 ; i < all.size() ; ++i ) equal = equal && all[i] == i ;
                for( auto const & chunk : data.chunks )
                {
                    auto const & outer = world_bounds( chunk.node ) ;
                    for( size_t i = chunk.first ; i < chunk.end ; ++i )
                    {
                        auto const & b = data.items[i].bounds->get_coarse_transformed() ;
                        auto expected = chunk.offset + data.items[i].local ;
                        if( step % 3 != 0 && i < count * 25 / 100 )
                            expected.y( expected.y() + .65f * std::sin( float_t( step ) * .8f + float_t( i % 113 ) * .19f ) ) ;
                        containment = containment && ( b.get_center() - expected ).length2() < 1e-6f ;
                        for( size_t a = 0 ; a < 3 ; ++a ) containment = containment &&
                            outer.get_min()[a] <= b.get_min()[a] + .001f && outer.get_max()[a] >= b.get_max()[a] - .001f ;
                    }
                }
            }
            check( "flat and hierarchy produce identical ordered IDs; no-cull retains all", equal ) ;
            check( "animated child bounds remain inside chunk bounds", containment ) ;
            camera_t camera ; auto const center = data.bounds().get_center() ;
            camera.make_perspective_fov( 100, 100, .8f, .1f, 10 ) ;
            camera.look_at( center + vec3_t( 0, 0, 1000 ), vec3_t( 0, 1, 0 ), center + vec3_t( 0, 0, 1100 ) ) ;
            auto stats = data.cull( camera.get_frustum(), mode::hierarchy, hierarchical ) ;
            check( "empty visibility rejects root without leaf tests", hierarchical.empty() && stats.group_tests == 1 && stats.leaf_tests == 0 ) ;
            float_t const dim = data.bounds().get_dims().length() + 10 ;
            camera.make_orthographic( dim, dim, .1f, dim * 4 ) ;
            camera.look_at( center + vec3_t( 0, 0, dim ), vec3_t( 0, 1, 0 ), center ) ;
            stats = data.cull( camera.get_frustum(), mode::hierarchy, hierarchical ) ;
            check( "fully contained root accepts all without leaf tests", hierarchical.size() == count && stats.accepted_groups == 1 && stats.leaf_tests == 0 ) ;
        }
        camera_t camera ; camera_for( camera, rectangle{ 0, 0, 1280, 800 }, data.bounds(), 5, 45, 1.05f ) ;
        for( auto const method : { mode::none, mode::flat, mode::hierarchy } )
        {
            motor::vector<double_t> times ; counts stats ;
            for( size_t i = 0 ; i < 35 ; ++i )
            {
                auto const start = clock_t::now() ; stats = data.cull( camera.get_frustum(), method, hierarchical ) ;
                if( i >= 5 ) times.emplace_back( elapsed( start ) ) ;
            }
            std::sort( times.begin(), times.end() ) ;
            motor::log::global_t::status( motor::string_t( "[culling stress CPU] " ) + names[size_t( method )] +
                " objects=" + motor::to_string( requested ) + " submitted=" + motor::to_string( hierarchical.size() ) +
                " median_ms=" + motor::to_string( ( times[14] + times[15] ) * .5 ) +
                " p95_ms=" + motor::to_string( times[28] ) + " leaf_tests=" + motor::to_string( stats.leaf_tests ) +
                " group_tests=" + motor::to_string( stats.group_tests ) ) ;
        }
        return ok ;
    }
}

int run_culling_stress( int argc, char ** argv ) ;
