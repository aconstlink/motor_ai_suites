#pragma once

#include <motor/gfx/camera/generic_camera.h>
#include <motor/collide/3d/transform.hpp>
#include <motor/log/global.h>
#include <algorithm>
#include <cmath>

namespace shadow_sample
{
    using namespace motor::core::types;

    inline motor::math::vec3f_t sun_direction( float_t const seconds = 0.0f ) noexcept
    {
        // A slow lateral sweep, always above the courtyard; smooth at both turnarounds.
        float_t const phase = seconds * ( 6.2831853f / 80.0f );
        return motor::math::vec3f_t( 0.8f + 0.2f * std::sin( phase ), 1.0f, 0.6f ).normalized();
    }

    inline void_t sponza_camera( motor::gfx::generic_camera_ref_t camera,
        motor::collide::aabb3f_cref_t bounds, float_t const aspect, float_t const seconds ) noexcept
    {
        auto const center = bounds.get_center();
        auto const dims = bounds.get_dims();
        float_t const phase = seconds * ( 6.2831853f / 40.0f );
        // Rise towards the upper arcade, then descend, with a smooth loop seam.
        float_t const rise = 0.5f - 0.5f * std::cos( phase );
        auto const eye = motor::math::vec3f_t( center.x() + dims.x() * 0.25f * std::sin( phase ),
            bounds.get_min().y() + 1.65f + dims.y() * 0.40f * rise,
            center.z() + 0.55f * std::cos( phase ) );
        auto const target = eye + motor::math::vec3f_t( 4.0f * std::cos( phase ),
            1.2f - 2.4f * rise, -3.0f * std::sin( phase ) );
        // Keep a useful horizontal view in portrait, not a cropped landscape projection.
        float_t const vertical_fov = 2.0f * std::atan( std::tan( 0.50f ) / std::min( aspect, 1.0f ) );
        camera.make_perspective_fov( aspect, 1.0f, vertical_fov, 0.08f,
            std::max( dims.length() * 2.0f, 100.0f ) );
        camera.look_at( eye, motor::math::vec3f_t( 0, 1, 0 ), target );
    }

    inline void_t fit_light( motor::gfx::generic_camera_ref_t camera,
        motor::collide::aabb3f_cref_t bounds, motor::math::vec3f_cref_t light_direction ) noexcept
    {
        auto const direction = light_direction.normalized().negated();
        auto const up = std::abs( direction.y() ) > 0.99f ?
            motor::math::vec3f_t( 0, 0, 1 ) : motor::math::vec3f_t( 0, 1, 0 );
        camera.look_at( motor::math::vec3f_t(), up, direction );
        auto const box = motor::collide::transform_bounds3f_t::to_aabb(
            motor::collide::transform3f_t::from_aabb( bounds,
                motor::math::m3d::trafof_t( camera.get_view_matrix() ) ) );
        float_t const margin = 0.5f, near_plane = 0.1f;
        auto const dims = box.get_dims();
        camera.make_orthographic( dims.x() + 2 * margin, dims.y() + 2 * margin,
            near_plane, near_plane + dims.z() + 2 * margin );
        auto const center = box.get_center();
        auto const position = motor::math::vec3f_t(
            center.x(), center.y(), box.get_max().z() + near_plane + margin );
        camera.translate_to( ( camera.get_camera_matrix() *
            motor::math::vec4f_t( position, 1.0f ) ).xyz() );
    }

    inline void_t fit_view( motor::gfx::generic_camera_ref_t camera,
        motor::collide::aabb3f_cref_t bounds, float_t const aspect,
        motor::math::vec3f_cref_t eye_direction ) noexcept
    {
        float_t const fov = 0.72f;
        float_t const half_angle = std::atan( std::tan( fov * 0.5f ) * std::min( aspect, 1.0f ) );
        float_t const radius = std::max( bounds.get_dims().length() * 0.5f, 0.1f );
        float_t const distance = radius / std::sin( half_angle ) * 1.06f;
        camera.make_perspective_fov( aspect, 1.0f, fov, 0.1f, distance + radius * 3.0f );
        camera.look_at( bounds.get_center() + eye_direction.normalized() * distance,
            motor::math::vec3f_t( 0, 1, 0 ), bounds.get_center() );
    }

    inline bool_t encloses( motor::gfx::generic_camera_cref_t camera,
        motor::collide::aabb3f_cref_t box ) noexcept
    {
        auto const vp = camera.get_proj_matrix() * camera.get_view_matrix();
        auto const lo = box.get_min(), hi = box.get_max();
        for( size_t i = 0; i < 8; ++i )
        {
            auto const clip = vp * motor::math::vec4f_t( ( i & 1 ) ? hi.x() : lo.x(),
                ( i & 2 ) ? hi.y() : lo.y(), ( i & 4 ) ? hi.z() : lo.z(), 1.0f );
            if( !std::isfinite( clip.w() ) || clip.w() <= 0.0f ) return false;
            for( size_t axis = 0; axis < 3; ++axis )
                if( !std::isfinite( clip[axis] ) || std::abs( clip[axis] / clip.w() ) > 1.0001f ) return false;
        }
        return true;
    }
}
