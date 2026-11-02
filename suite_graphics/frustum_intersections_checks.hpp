#pragma once
#include "frustum_intersections.hpp"
#include <motor/log/global.h>

namespace intersection_sample
{
    inline bool_t cpu_checks( void_t )
    {
        bool_t passed = true ;
        auto const check = [&]( char const * name, bool_t const condition )
        {
            motor::log::global_t::test( motor::string_t( "[frustum] " ) + name, condition ) ;
            passed = condition && passed ;
        } ;
        frustum_t const unit( mat4_t::make_identity(), mat4_t::make_identity() ) ;
        {
            bool_t ok = true ;
            for( size_t axis = 0 ; axis < 3 ; ++axis )
            {
                for( float_t const sign : { -1.0f, 1.0f } )
                {
                    for( shape const type : { shape::sphere, shape::aabb, shape::obb } )
                    {
                        object obj ; obj.type = type ; obj.radius = .5f ; obj.extent = vec3_t( .5f ) ;
                        ok &= classify( unit, obj ) == result_t::inside ;
                        obj.center[axis] = sign * 1.5f ;
                        ok &= classify( unit, obj ) == result_t::intersect ;
                        obj.center[axis] = sign * 2 ;
                        ok &= classify( unit, obj ) == result_t::outside ;
                        obj.center[axis] = sign * .5f ;
                        ok &= classify( unit, obj ) == result_t::intersect ;
                    }
                }
            }
            check( "sphere/AABB/OBB: inside, all six rejecting planes, internal/external tangency", ok ) ;
        }
        {
            bool_t ok = true ;
            for( shape const type : { shape::sphere, shape::aabb, shape::obb } )
            {
                object obj ; obj.type = type ; obj.radius = 2 ; obj.extent = vec3_t( 2 ) ;
                ok &= classify( unit, obj ) == result_t::intersect ;
                obj.center = vec3_t( -1, 4, 0 ) ;
                ok &= classify( unit, obj ) == result_t::outside ;
            }
            check( "enclosing volumes and later separating plane", ok ) ;
            check( "sphere/corner case intentionally conservative",
                hit_t::hit_test_frustum_sphere( unit, hit_t::sphere_t( vec3_t( 1.75f, 1.75f, 0 ), 1 ) ) == result_t::intersect ) ;
        }
        {
            bool_t ok = true ;
            for( size_t axis = 0 ; axis < 3 ; ++axis )
            {
                for( float_t const sign : { -1.0f, 1.0f } )
                {
                    vec3_t direction ; direction[axis] = sign ;
                    vec3_t parallel ; parallel[( axis + 1 ) % 3] = 1 ;
                    ok &= hit_t::hit_test_frustum_point( unit, direction ) == result_t::inside ;
                    ok &= hit_t::hit_test_frustum_point( unit, direction * 1.01f ) == result_t::outside ;
                    float_t t = -99 ;
                    ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( direction * 3, direction * -2 ), t ) == result_t::intersect && std::abs( t - 1 ) < 1e-5f ;
                    ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( vec3_t( 0 ), direction ), t ) == result_t::intersect && std::abs( t - 1 ) < 1e-5f ;
                    ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( direction * 3, direction ), t ) == result_t::outside && t == 0 ;
                    ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( direction * 3, parallel ), t ) == result_t::outside && t == 0 ;
                    ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( direction, parallel ), t ) == result_t::intersect && t == 0 ;
                }
            }
            float_t t = -99 ;
            ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( vec3_t( 0 ), vec3_t( 0 ) ), t ) == result_t::outside && t == 0 ;
            ok &= hit_t::hit_test_frustum_ray( unit, motor::math::ray3f_t( vec3_t( 2, 0, 0 ), vec3_t( -1, 1, 0 ) ), t ) == result_t::intersect && t == 1 ;
            check( "point boundaries; ray entry/exit, tangency, parallel, backwards and zero direction", ok ) ;
        }
        {
            bool_t ok = true ;
            scene data ; data.animate( 2.7f, 3 ) ;
            camera_t camera ;
            make_camera( camera, rectangle{ 0, 0, 1000, 800 }, 2.7f, 0, .8f, 18, false ) ;
            auto const & frustum = camera.get_frustum() ;
            for( auto const & obj : data.objects )
            {
                auto const box = hit_t::obb_t( obj.center, obj.extent, obj.axes ) ;
                auto expected = result_t::inside ;
                for( size_t i = 0 ; i < 6 ; ++i )
                {
                    auto const & plane = frustum.get_plane( i ) ;
                    bool_t all_outside = true ;
                    for( auto const & p : box.points() )
                    {
                        float_t const d = plane.xyz().dot( p ) + plane.w() ;
                        all_outside &= d > 0 ;
                        if( d >= 0 ) expected = result_t::intersect ;
                    }
                    if( all_outside ) { expected = result_t::outside ; break ; }
                }
                ok &= hit_t::hit_test_frustum_obb( frustum, box ) == expected ;
            }
            check( "rotated OBB projections agree with independent eight-corner plane test", ok ) ;
        }
        {
            bool_t projection_ok = true ;
            for( bool_t const ortho : { false, true } )
            {
                camera_t camera ; make_camera( camera, rectangle{ 0, 0, 1000, 800 }, 0, 0, .8f, 18, ortho ) ;
                auto const & projection = camera.mat_proj() ;
                for( size_t i = 0 ; i < 2 ; ++i )
                {
                    vec4_t const local( 0, 0, i == 0 ? -.6f : -18.0f, 1 ) ;
                    auto const clip = projection * local ;
                    projection_ok &= clip.w() > 0 && std::isfinite( clip.z() ) &&
                        std::abs( clip.z() / clip.w() - ( i == 0 ? -1.0f : 1.0f ) ) < 1e-4f ;
                }
            }
            check( "perspective/orthographic cameras retain motor's [-1,+1] near/far convention", projection_ok ) ;
        }
        {
            scene data ; data.animate( 0, 0 ) ;
            camera_t camera ; make_camera( camera, rectangle{ 0, 0, 1000, 800 }, 0, 0, .8f, 18, false ) ;
            data.cull( camera.get_frustum() ) ;
            auto const expected = data.visible ;
            auto far_view = mat4_t::make_identity() ; far_view.set_row( 0, vec4_t( 1, 0, 0, 1000 ) ) ;
            data.cull( frustum_t( far_view, mat4_t::make_identity() ) ) ;
            bool_t const empty = data.visible.empty() && data.inside == 0 && data.touching == 0 ;
            data.cull( camera.get_frustum() ) ;
            check( "visible list clears completely and restores without stale object IDs", empty && !expected.empty() && data.visible == expected ) ;
        }
        {
            bool_t layout_ok = true, visibility_ok = true, observer_ok = true, changes = false ;
            for( auto const size : { motor::math::vec2ui_t( 1280, 800 ), motor::math::vec2ui_t( 1080, 1080 ),
                motor::math::vec2ui_t( 1080, 1920 ), motor::math::vec2ui_t( 720, 1280 ), motor::math::vec2ui_t( 64, 64 ) } )
            {
                auto const areas = layout( size.x(), size.y() ) ;
                for( auto const & r : areas )
                    layout_ok &= r.width > 0 && r.height > 0 && r.x + r.width <= size.x() && r.y + r.height <= size.y() ;
                layout_ok &= areas[0].x + areas[0].width <= areas[1].x || areas[0].y + areas[0].height <= areas[1].y ;
                scene data ; motor::vector<size_t> previous ;
                for( size_t step = 0 ; step < 24 ; ++step )
                {
                    float_t const time = float_t( step ) * 4 ;
                    camera_t camera, observer ;
                    make_camera( camera, areas[0], time, 0, .8f, 18, step >= 12 ) ;
                    auto const & frustum = camera.get_frustum() ;
                    data.animate( time, int_t( step % 6 ) ) ; data.cull( frustum ) ;
                    size_t kept = 0 ;
                    for( size_t i = 0 ; i < data.objects.size() ; ++i )
                    {
                        bool_t const submitted = std::find( data.visible.begin(), data.visible.end(), i ) != data.visible.end() ;
                        visibility_ok &= submitted == ( classify( frustum, data.objects[i] ) != result_t::outside ) ;
                        if( submitted ) ++kept ;
                    }
                    visibility_ok &= kept == data.visible.size() && kept == data.inside + data.touching ;
                    changes |= step > 0 && previous != data.visible ; previous = data.visible ;
                    make_observer( observer, areas[1], frustum ) ;
                    corners_t corners ;
                    if( !frustum.points( corners ) ) { observer_ok = false ; continue ; }
                    for( auto const & p : corners )
                    {
                        auto const clip = observer.mat_proj() * observer.mat_view() * vec4_t( p, 1 ) ;
                        observer_ok &= std::isfinite( clip.x() ) && clip.w() > 0 &&
                            std::abs( clip.x() ) <= clip.w() && std::abs( clip.y() ) <= clip.w() && std::abs( clip.z() ) <= clip.w() ;
                    }
                }
            }
            check( "landscape/square/vertical and resize viewport layout", layout_ok ) ;
            check( "shared visible list contains exactly the accepted objects across animation/projections", visibility_ok && changes ) ;
            check( "observer framing contains every frustum corner in every format", observer_ok ) ;
        }
        return passed ;
    }
}
