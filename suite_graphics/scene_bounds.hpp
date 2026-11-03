#pragma once

#include "frustum_intersections.hpp"
#include <motor/scene/node/logic_group.h>
#include <motor/scene/node/leaf.h>
#include <motor/scene/component/bounds_component.hpp>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/memory/global.h>

namespace intersection_sample
{
    using bounds_t = motor::scene::bounds_component_t ;
    using trafo_t = motor::math::trafo3f_t ;
    using trafo_component_t = motor::scene::trafo3d_component_t ;
    using group_t = motor::scene::logic_group_t ;
    using leaf_t = motor::scene::leaf_t ;
    using node_t = motor::scene::node_t ;
    using aabb_t = motor::collide::aabb3f_t ;

    inline bool_t valid_bounds( aabb_t const & box ) noexcept
    {
        for( size_t axis = 0 ; axis < 3 ; ++axis )
            if( !std::isfinite( box.get_min()[axis] ) || !std::isfinite( box.get_max()[axis] ) ||
                box.get_min()[axis] > box.get_max()[axis] ) return false ;
        return true ;
    }

    inline aabb_t const & world_bounds( node_t const * node ) noexcept
    { return node->borrow_component<bounds_t>()->get_coarse_transformed() ; }

    inline void_t set_pose( node_t * node, trafo_t const & pose ) noexcept
    { node->borrow_component<trafo_component_t>()->set_trafo( pose ) ; }

    inline trafo_t pose( vec3_t const & translation, float_t const angle,
        vec3_t const & scale = vec3_t( 1 ) ) noexcept
    {
        trafo_t result ;
        result.set_scale( scale ) ;
        result.rotate_by_axis_fl( vec3_t( 0, 1, 0 ), angle ) ;
        result.set_translation( translation ) ;
        return result ;
    }

    // Only root is owned here; every other pointer is borrowed from its subtree.
    class bounds_scene
    {
    public:
        group_t * root = nullptr ;
        std::array<group_t *, 3> clusters {} ;
        std::array<group_t *, 9> rows {} ;
        std::array<leaf_t *, object_count> leaves {} ;
        motor::scene::trafo_visitor_t visitor ;
        size_t rejected_groups = 0, tested_leaves = 0 ;

        bounds_scene( void_t )
        {
            root = motor::shared( group_t() ) ;
            root->add_component( motor::shared( trafo_component_t() ) ) ;
            for( size_t c = 0 ; c < clusters.size() ; ++c )
            {
                auto cluster = motor::shared( group_t() ) ;
                clusters[c] = cluster ;
                cluster->add_component( motor::shared( trafo_component_t() ) ) ;
                root->add_child( motor::move( cluster ) ) ;
                for( size_t r = 0 ; r < 3 ; ++r )
                {
                    size_t const row_id = c * 3 + r ;
                    auto row = motor::shared( group_t() ) ;
                    rows[row_id] = row ;
                    row->add_component( motor::shared( trafo_component_t() ) ) ;
                    clusters[c]->add_child( motor::move( row ) ) ;
                    for( size_t k = 0 ; k < 7 ; ++k )
                    {
                        auto leaf = motor::shared( leaf_t() ) ;
                        leaves[row_id * 7 + k] = leaf ;
                        vec3_t const center( .2f, .3f, -.1f ), extent( .28f, .5f, .32f ) ;
                        leaf->add_component( motor::shared( bounds_t( aabb_t( center + extent, center - extent ) ) ) ) ;
                        leaf->add_component( motor::shared( trafo_component_t() ) ) ;
                        rows[row_id]->add_child( motor::move( leaf ) ) ;
                    }
                }
            }
        }

        bounds_scene( bounds_scene const & ) = delete ;
        bounds_scene & operator=( bounds_scene const & ) = delete ;
        ~bounds_scene( void_t ) { motor::release( motor::move( root ) ) ; }

        void_t update( void_t )
        { node_t::traverser( root ).apply( &visitor ) ; }

        void_t animate( float_t const time, scene & output )
        {
            set_pose( root, pose( vec3_t( 0 ), .08f * std::sin( time * .2f ) ) ) ;
            for( size_t c = 0 ; c < clusters.size() ; ++c )
            {
                float_t const phase = time * .45f + float_t( c ) * 2 ;
                float_t const spread = .85f + .2f * std::sin( phase ) ;
                set_pose( clusters[c], pose( vec3_t( ( float_t( c ) - 1 ) * 7, .9f, 0 ),
                    .2f * std::sin( phase ), vec3_t( spread, 1, spread ) ) ) ;
                for( size_t r = 0 ; r < 3 ; ++r )
                {
                    size_t const row_id = c * 3 + r ;
                    set_pose( rows[row_id], pose( vec3_t( 0, 0, ( float_t( r ) - 1 ) * 4 ),
                        .12f * std::sin( phase + float_t( r ) ) ) ) ;
                    for( size_t k = 0 ; k < 7 ; ++k )
                    {
                        size_t const i = row_id * 7 + k ;
                        float_t const a = time * .6f + float_t( i ) * .4f ;
                        set_pose( leaves[i], pose( vec3_t( ( float_t( k ) - 3 ) * .85f,
                            .35f * std::sin( a ), 0 ), a, vec3_t( 1, .8f + .3f * std::sin( a ), 1 ) ) ) ;
                    }
                }
            }
            update() ;
            for( size_t i = 0 ; i < leaves.size() ; ++i )
            {
                auto const & local = leaves[i]->borrow_component<bounds_t>()->get_coarse() ;
                auto const & world = leaves[i]->borrow_component<trafo_component_t>()->get_computed() ;
                auto & obj = output.objects[i] ;
                obj.type = shape::obb ;
                obj.center = ( world * vec4_t( local.get_center(), 1 ) ).xyz() ;
                obj.extent = local.get_extend() ;
                obj.axes = world.get_rotation_matrix() ;
            }
        }

        void_t cull( frustum_t const & frustum, scene & output )
        {
            output.visible.clear() ; output.inside = 0 ; output.touching = 0 ;
            rejected_groups = 0 ; tested_leaves = 0 ;
            for( auto & obj : output.objects ) obj.result = result_t::outside ;
            auto const rejected = [&]( node_t const * node )
            {
                auto const & box = world_bounds( node ) ;
                bool_t const outside = !valid_bounds( box ) ||
                    hit_t::hit_test_frustum_aabb( frustum, box ) == result_t::outside ;
                if( outside ) ++rejected_groups ;
                return outside ;
            } ;
            if( rejected( root ) ) return ;
            for( size_t c = 0 ; c < clusters.size() ; ++c )
            {
                if( rejected( clusters[c] ) ) continue ;
                for( size_t r = 0 ; r < 3 ; ++r )
                {
                    size_t const row_id = c * 3 + r ;
                    if( rejected( rows[row_id] ) ) continue ;
                    for( size_t k = 0 ; k < 7 ; ++k )
                    {
                        size_t const i = row_id * 7 + k ;
                        ++tested_leaves ;
                        auto & obj = output.objects[i] ;
                        obj.result = hit_t::hit_test_frustum_aabb( frustum, world_bounds( leaves[i] ) ) ;
                        if( obj.result == result_t::outside ) continue ;
                        output.visible.emplace_back( i ) ;
                        if( obj.result == result_t::inside ) ++output.inside ; else ++output.touching ;
                    }
                }
            }
        }
    } ;
}
