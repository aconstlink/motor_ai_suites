#include "scene_bounds.hpp"
#include <motor/log/global.h>
#include <limits>

namespace bounds_test
{
    using namespace intersection_sample ;

    struct checks
    {
        size_t count = 0, failures = 0 ;
        void_t check( bool_t const ok, motor::string_cref_t name )
        {
            ++count ;
            if( !ok ) { ++failures ; motor::log::global_t::test( "[bounds] " + name, false ) ; }
        }
    } ;

    bool_t equal( aabb_t const & a, aabb_t const & b ) noexcept
    {
        if( !valid_bounds( a ) || !valid_bounds( b ) ) return false ;
        for( size_t k = 0 ; k < 3 ; ++k )
        {
            float_t const tolerance = 2e-4f * ( 1 + std::max( std::abs( b.get_min()[k] ), std::abs( b.get_max()[k] ) ) ) ;
            if( std::abs( a.get_min()[k] - b.get_min()[k] ) > tolerance ||
                std::abs( a.get_max()[k] - b.get_max()[k] ) > tolerance ) return false ;
        }
        return true ;
    }

    // Deliberately independent of aabb::merge, points(), and the OBB conversion.
    void_t include( aabb_t & box, vec3_t const & point )
    {
        for( size_t k = 0 ; k < 3 ; ++k )
        {
            box.get_min()[k] = std::min( box.get_min()[k], point[k] ) ;
            box.get_max()[k] = std::max( box.get_max()[k], point[k] ) ;
        }
    }

    aabb_t reference( aabb_t const & local, mat4_t const & world )
    {
        aabb_t result ;
        for( size_t corner = 0 ; corner < 8 ; ++corner )
        {
            vec3_t p ;
            for( size_t k = 0 ; k < 3 ; ++k )
                p[k] = corner & ( size_t( 1 ) << k ) ? local.get_max()[k] : local.get_min()[k] ;
            include( result, ( world * vec4_t( p, 1 ) ).xyz() ) ;
        }
        return result ;
    }

    void_t merge_and_transform( checks & test )
    {
        aabb_t const a( vec3_t( -2, -3, -4 ), vec3_t( -8, -9, -10 ) ) ;
        aabb_t const b( vec3_t( 6, 4, 2 ), vec3_t( 1, -5, -12 ) ) ;
        aabb_t box ;
        test.check( &box.merge( a ) == &box && equal( box, a ), "empty merge returns self and keeps negative bounds" ) ;
        box.merge( b ) ;
        test.check( equal( box, aabb_t( vec3_t( 6, 4, 2 ), vec3_t( -8, -9, -12 ) ) ), "union uses component-wise min/max" ) ;
        auto const before = box ;
        box.merge( aabb_t() ) ; box.merge( box ) ;
        test.check( equal( box, before ), "empty and self merge do not change populated bounds" ) ;
        {
            bounds_t component( a ) ;
            for( auto const scale : { vec3_t( 1 ), vec3_t( 2, .5f, 3 ), vec3_t( -2, .5f, -3 ) } )
            {
                auto const t = pose( vec3_t( 9, -4, 7 ), .73f, scale ) ;
                component.transform( t ) ;
                test.check( equal( component.get_coarse_transformed(), reference( a, t.get_transformation() ) ),
                    "off-center transformed bounds match eight corners (including mirrored scale)" ) ;
            }
            auto const transformed = component.get_coarse_transformed() ;
            bounds_t moved( std::move( component ) ) ;
            test.check( equal( moved.get_coarse(), a ) && equal( moved.get_coarse_transformed(), transformed ),
                "move preserves local and world bounds" ) ;
        }
    }

    void_t hierarchy( checks & test )
    {
        bounds_scene graph ; scene data ;
        for( size_t step = 0 ; step < 32 ; ++step )
        {
            graph.animate( float_t( step ) * .7f, data ) ;
            aabb_t root_expected ;
            auto const root_matrix = graph.root->borrow_component<trafo_component_t>()->get_trafo().get_transformation() ;
            for( size_t c = 0 ; c < graph.clusters.size() ; ++c )
            {
                aabb_t cluster_expected ;
                auto const cluster_matrix = root_matrix * graph.clusters[c]->borrow_component<trafo_component_t>()->get_trafo().get_transformation() ;
                for( size_t r = 0 ; r < 3 ; ++r )
                {
                    size_t const row_id = c * 3 + r ;
                    aabb_t row_expected ;
                    auto const row_matrix = cluster_matrix * graph.rows[row_id]->borrow_component<trafo_component_t>()->get_trafo().get_transformation() ;
                    for( size_t k = 0 ; k < 7 ; ++k )
                    {
                        auto * leaf = graph.leaves[row_id * 7 + k] ;
                        auto const & local = leaf->borrow_component<bounds_t>()->get_coarse() ;
                        auto const world = row_matrix * leaf->borrow_component<trafo_component_t>()->get_trafo().get_transformation() ;
                        auto const expected = reference( local, world ) ;
                        test.check( equal( world_bounds( leaf ), expected ), "leaf bounds match independently composed corner reference" ) ;
                        include( row_expected, expected.get_min() ) ; include( row_expected, expected.get_max() ) ;
                    }
                    test.check( equal( world_bounds( graph.rows[row_id] ), row_expected ), "row bounds match its seven children" ) ;
                    include( cluster_expected, row_expected.get_min() ) ; include( cluster_expected, row_expected.get_max() ) ;
                }
                test.check( equal( world_bounds( graph.clusters[c] ), cluster_expected ), "cluster bounds bubble up across nested groups" ) ;
                include( root_expected, cluster_expected.get_min() ) ; include( root_expected, cluster_expected.get_max() ) ;
            }
            test.check( equal( world_bounds( graph.root ), root_expected ), "root matches all leaves, without repeated world transformation" ) ;
            auto * cached = graph.root->borrow_component<bounds_t>() ;
            graph.update() ;
            test.check( cached == graph.root->borrow_component<bounds_t>() && equal( world_bounds( graph.root ), root_expected ),
                "repeat traversal neither grows bounds nor replaces the component" ) ;
            for( bool_t const ortho : { false, true } )
            {
                camera_t cam ; make_camera( cam, rectangle{ 0, 0, 900, 700 }, float_t( step ), 0, .82f, 18, ortho ) ;
                graph.cull( cam.get_frustum(), data ) ;
                motor::vector<size_t> expected_ids ;
                for( size_t i = 0 ; i < graph.leaves.size() ; ++i )
                    if( hit_t::hit_test_frustum_aabb( cam.get_frustum(), world_bounds( graph.leaves[i] ) ) != result_t::outside )
                        expected_ids.emplace_back( i ) ;
                test.check( data.visible == expected_ids, "hierarchical culling matches flat world-AABB culling" ) ;
            }
        }
        auto far_view = mat4_t::make_identity() ; far_view.set_row( 0, vec4_t( 1, 0, 0, 1000 ) ) ;
        graph.cull( frustum_t( far_view, mat4_t::make_identity() ), data ) ;
        test.check( data.visible.empty() && graph.tested_leaves == 0, "outside root skips all leaf tests" ) ;
    }

    void_t shrink_empty_and_depth( checks & test )
    {
        group_t root ;
        auto * parent = &root ;
        // Exceed both stacks' initial capacity, with groups that have no local transform.
        for( size_t i = 0 ; i < 24 ; ++i )
        {
            auto child = motor::shared( group_t() ) ;
            auto * borrowed = child.mtr() ; parent->add_child( motor::move( child ) ) ; parent = borrowed ;
        }
        auto leaf = motor::shared( leaf_t() ) ;
        auto * borrowed_leaf = leaf.mtr() ;
        aabb_t const local( vec3_t( 1 ), vec3_t( -1 ) ) ;
        leaf->add_component( motor::shared( bounds_t( local ) ) ) ;
        leaf->add_component( motor::shared( trafo_component_t() ) ) ;
        parent->add_child( motor::move( leaf ) ) ;
        motor::scene::trafo_visitor_t visitor ;
        auto const update = [&] ( void_t ) { node_t::traverser( &root ).apply( &visitor ) ; } ;
        set_pose( borrowed_leaf, pose( vec3_t( 30, 0, 0 ), 0, vec3_t( 5 ) ) ) ; update() ;
        test.check( equal( world_bounds( &root ), aabb_t( vec3_t( 35, 5, 5 ), vec3_t( 25, -5, -5 ) ) ), "deep tree retains translated/scaled child" ) ;
        set_pose( borrowed_leaf, trafo_t() ) ; update() ;
        test.check( equal( world_bounds( &root ), local ), "bounds shrink after motion and scale reduction" ) ;
        aabb_t const smaller( vec3_t( .25f ), vec3_t( -.25f ) ) ;
        borrowed_leaf->borrow_component<bounds_t>()->set_coarse( smaller ) ; update() ;
        test.check( equal( world_bounds( &root ), smaller ), "bounds shrink after local geometry bounds change" ) ;
        parent->detach( borrowed_leaf ) ;
        motor::release( motor::move( borrowed_leaf ) ) ; update() ;
        test.check( !valid_bounds( world_bounds( &root ) ), "removing last child clears old group bounds" ) ;
        {
            auto late = motor::shared( leaf_t() ) ;
            auto * borrowed = late.mtr() ; parent->add_child( motor::move( late ) ) ; update() ;
            test.check( !valid_bounds( world_bounds( &root ) ), "unbounded debug leaf keeps the subtree empty" ) ;
            borrowed->add_component( motor::shared( bounds_t( local ) ) ) ; update() ;
            test.check( equal( world_bounds( &root ), local ), "late bounds attachment restores the subtree" ) ;
        }
        {
            leaf_t standalone ;
            standalone.add_component( motor::shared( bounds_t( local ) ) ) ;
            node_t::traverser( &standalone ).apply( &visitor ) ;
            test.check( equal( world_bounds( &standalone ), local ), "same visitor handles a standalone leaf after a deep tree" ) ;
        }
    }
}

int main( motor::core::types::void_t )
{
    bool passed = false ;
    {
        bounds_test::checks test ;
        bounds_test::merge_and_transform( test ) ;
        bounds_test::hierarchy( test ) ;
        bounds_test::shrink_empty_and_depth( test ) ;
        passed = test.failures == 0 ;
        motor::log::global_t::test( "[bounds] " + motor::to_string( test.count ) + " checks, " +
            motor::to_string( test.failures ) + " failures", passed ) ;
    }
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std() == 0 ;
    return passed && clean ? 0 : 1 ;
}
