#pragma once

#include <motor/collide/3d/hit_test.hpp>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/std/vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace intersection_sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using mat3_t = motor::math::mat3f_t ;
    using mat4_t = motor::math::mat4f_t ;
    using camera_t = motor::gfx::generic_camera_t ;
    using frustum_t = motor::math::frustum3f_t ;
    using hit_t = motor::collide::c3d::hit_test<float_t> ;
    using result_t = motor::collide::hit_test_type ;
    using corners_t = std::array<vec3_t, 8> ;
    enum class shape { sphere, aabb, obb, ray, point } ;
    inline constexpr char const * modes[] = { "Mixed volumes", "Spheres", "AABBs", "OBBs", "Rays", "Points", "Scene bounds" } ;
    inline constexpr int_t mode_count = 7 ;
    inline constexpr size_t object_count = 63 ;
    inline constexpr float_t pi = 3.14159265359f ;

    struct rectangle
    {
        uint_t x = 0, y = 0, width = 1, height = 1 ; // Top-left pixel coordinates.
        motor::math::vec4ui_t viewport( uint_t const window_height ) const noexcept
        { return motor::math::vec4ui_t( x, window_height - y - height, width, height ) ; }
    } ;

    inline std::array<rectangle, 2> layout( uint_t const width, uint_t const height ) noexcept
    {
        uint_t const margin = std::min( 12u, std::min( width, height ) / 16 ) ;
        uint_t const top = std::min( 64u, height / 6 ) ;
        uint_t const gap = margin ;
        uint_t const w = width - 2 * margin, h = height - top - margin ;
        if( float_t( width ) > float_t( height ) * 1.25f )
        {
            uint_t const half = ( w - gap ) / 2 ;
            return { rectangle{ margin, top, half, h },
                rectangle{ margin + half + gap, top, w - half - gap, h } } ;
        }
        uint_t const half = ( h - gap ) / 2 ;
        return { rectangle{ margin, top, w, half },
            rectangle{ margin, top + half + gap, w, h - half - gap } } ;
    }

    struct object
    {
        shape type = shape::sphere ;
        vec3_t center, extent = vec3_t( .65f, .8f, .55f ) ;
        mat3_t axes = mat3_t::make_identity() ;
        vec3_t direction = vec3_t( 0, 0, -1 ) ;
        float_t radius = .8f, hit_parameter = 0 ;
        result_t result = result_t::outside ;
    } ;

    inline result_t classify( frustum_t const & frustum, object & obj ) noexcept
    {
        switch( obj.type )
        {
        case shape::sphere:
            return hit_t::hit_test_frustum_sphere( frustum, hit_t::sphere_t( obj.center, obj.radius ) ) ;
        case shape::aabb:
            return hit_t::hit_test_frustum_aabb( frustum,
                hit_t::aabb_t( obj.center + obj.extent, obj.center - obj.extent ) ) ;
        case shape::obb:
            return hit_t::hit_test_frustum_obb( frustum, hit_t::obb_t( obj.center, obj.extent, obj.axes ) ) ;
        case shape::ray:
            return hit_t::hit_test_frustum_ray( frustum,
                motor::math::ray3f_t( obj.center, obj.direction ), obj.hit_parameter ) ;
        case shape::point:
            return hit_t::hit_test_frustum_point( frustum, obj.center ) ;
        }
        return result_t::outside ;
    }

    struct scene
    {
        std::array<object, object_count> objects ;
        motor::vector<size_t> visible ;
        size_t inside = 0, touching = 0 ;

        void_t animate( float_t const time, int_t const mode ) noexcept
        {
            for( size_t i = 0 ; i < objects.size() ; ++i )
            {
                auto & obj = objects[i] ;
                float_t const phase = float_t( i ) * .73f ;
                obj.type = shape( mode == 0 ? i % 3 : size_t( mode - 1 ) ) ;
                obj.center = vec3_t( ( float_t( i % 9 ) - 4 ) * 2.3f + .3f * std::sin( time * .4f + phase ),
                    1.3f + .45f * std::sin( time * .6f + phase ), ( float_t( i / 9 ) - 3 ) * 2.4f ) ;
                obj.extent = vec3_t( .65f, .8f, .55f ) ;
                float_t const angle = time * .28f + phase ;
                float_t const c = std::cos( angle ), s = std::sin( angle ) ;
                obj.axes.set_column( 0, vec3_t( c, 0, -s ) ) ;
                obj.axes.set_column( 1, vec3_t( 0, 1, 0 ) ) ;
                obj.axes.set_column( 2, vec3_t( s, 0, c ) ) ;
                obj.direction = vec3_t( .8f * std::sin( angle ), .2f * std::cos( phase ), -1 ).normalized() ;
            }
        }

        void_t cull( frustum_t const & frustum )
        {
            visible.clear() ; inside = 0 ; touching = 0 ;
            for( size_t i = 0 ; i < objects.size() ; ++i )
            {
                auto & obj = objects[i] ;
                obj.result = classify( frustum, obj ) ;
                if( obj.result == result_t::outside ) continue ;
                visible.emplace_back( i ) ;
                if( obj.result == result_t::inside ) ++inside ; else ++touching ;
            }
        }
    } ;

    inline void_t make_camera( camera_t & camera, rectangle const & area, float_t const time,
        float_t const yaw, float_t const fov, float_t const far_plane, bool_t const orthographic ) noexcept
    {
        float_t const aspect = float_t( area.width ) / float_t( area.height ) ;
        if( orthographic ) camera.make_orthographic( 9.0f * aspect, 9.0f, .6f, far_plane ) ;
        else camera.make_perspective_fov( float_t( area.width ), float_t( area.height ), fov, .6f, far_plane ) ;
        camera.look_at( vec3_t( std::sin( time * .19f ) * 5, 4.6f, 13.5f ), vec3_t( 0, 1, 0 ),
            vec3_t( yaw * 8 + std::sin( time * .27f ) * 3, 1.3f, -1.5f ) ) ;
    }

    inline void_t make_observer( camera_t & camera, rectangle const & area,
        frustum_t const & frustum ) noexcept
    {
        camera.look_at( vec3_t( 23, 21, 27 ), vec3_t( 0, 1, 0 ), vec3_t( 0, 2, 1 ) ) ;
        float_t const aspect = float_t( area.width ) / float_t( area.height ) ;
        float_t half_height = 1 ;
        auto const include = [&]( vec3_t const & p )
        {
            auto const local = camera.mat_view() * vec4_t( p, 1 ) ;
            half_height = std::max( half_height, std::max( std::abs( local.y() ), std::abs( local.x() ) / aspect ) ) ;
        } ;
        corners_t corners ;
        if( frustum.points( corners ) ) for( auto const & p : corners ) include( p ) ;
        auto const domain = hit_t::aabb_t( vec3_t( 11, 4, 15 ), vec3_t( -11, 0, -9 ) ).points() ;
        for( auto const & p : domain ) include( p ) ;
        // Fit both the room-sized object field and the complete test frustum after resize.
        half_height *= 1.16f ;
        camera.make_orthographic( half_height * 2 * aspect, half_height * 2, .1f, 180 ) ;
    }
}
