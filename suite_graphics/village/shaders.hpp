#pragma once
#include <motor/std/string>

namespace village
{
    // Shared deformation keeps the dragon silhouette identical in color and shadow passes.
    inline char const * surface_vertex_shader = R"(
            vertex_shader
            {
                in vec3_t pos : position ;
                in vec3_t normal : normal ;
                inout vec4_t color : color ;
                out vec4_t pos : position ;
                out vec3_t world_pos : texcoord0 ;
                out vec3_t world_normal : texcoord1 ;
                out vec4_t light_pos : texcoord2 ;
                out float_t shadow_depth : texcoord3 ;
                mat4_t u_world : world ;
                mat4_t u_view : view ;
                mat4_t u_proj : projection ;
                mat4_t u_light_view : light_view ;
                mat4_t u_light_proj : light_projection ;
                vec4_t u_clock ;
                void main()
                {
                    vec3_t p = in.pos ; vec3_t n = in.normal ;
                    if( u_clock.z > 0.5 )
                    {
                        if( in.color.w < -0.5 && in.color.w > -2.5 )
                        {
                            float_t side = 1.0 ; if( in.color.w < -1.5 ) side = -1.0 ;
                            float_t flap = side * ( 0.12 + sin( u_clock.x * 3.6 ) * 0.48 ) ;
                            vec3_t pivot = vec3_t( side * 0.65, 0.25, 0.40 ) ;
                            vec3_t q = p - pivot ;
                            p = pivot + vec3_t( cos( flap ) * q.x - sin( flap ) * q.y, sin( flap ) * q.x + cos( flap ) * q.y, q.z ) ;
                            n = vec3_t( cos( flap ) * n.x - sin( flap ) * n.y, sin( flap ) * n.x + cos( flap ) * n.y, n.z ) ;
                        }
                        if( in.color.w < -2.5 )
                        {
                            float_t q = max( -p.z - 1.1, 0.0 ) ;
                            float_t wave = u_clock.x * 2.3 + q * 1.8 ;
                            p.x = p.x + sin( wave ) * q * q * 0.08 ;
                            n.z = n.z + ( cos( wave ) * 1.8 * q * q + sin( wave ) * 2.0 * q ) * 0.08 * n.x ;
                        }
                    }
                    vec4_t world_pos = u_world * vec4_t( p, 1.0 ) ;
                    vec4_t world_normal = u_world * vec4_t( n, 0.0 ) ;
                    out.pos = u_proj * u_view * world_pos ;
                    out.light_pos = u_light_proj * u_light_view * world_pos ;
                    out.shadow_depth = ( out.pos.z / out.pos.w ) * 0.5 + 0.5 ;
                    out.world_pos = world_pos.xyz ; out.world_normal = world_normal.xyz ; out.color = vec4_t( in.color.xyz, max( in.color.w, 0.0 ) ) ;
                }
            }
    )" ;

    inline motor::string_t make_surface_shader( void_t )
    {
        return motor::string_t("config village_surface {")+surface_vertex_shader+R"(
            pixel_shader
            {
                inout vec4_t color : color ;
                in vec3_t world_pos : texcoord0 ;
                in vec3_t world_normal : texcoord1 ;
                in vec4_t light_pos : texcoord2 ;
                in float_t shadow_depth : texcoord3 ;
                vec3_t u_eye : camera_position ;
                vec4_t u_clock ;
                vec3_t u_material ;
                vec3_t u_direction_0 ;
                vec3_t u_direction_1 ;
                vec3_t u_energy_0 ;
                vec3_t u_energy_1 ;
                vec3_t u_energy_2 ;
                vec4_t u_fire ;
                tex2d_t u_shadow_map : light_shadow_map ;
                vec3_t u_shadow_settings ;
                void main()
                {
                    vec3_t n = normalize( in.world_normal ) ;
                    vec3_t v = normalize( u_eye - in.world_pos ) ;
                    vec3_t key = normalize( -u_direction_0 ) ;
                    float_t k = max( dot( n, key ), 0.0 ) ;
                    float_t visibility = 1.0 ;
                    vec3_t shadow_pos = ( in.light_pos.xyz / in.light_pos.w ) * 0.5 + 0.5 ;
                    if( u_shadow_settings.x > 0.5 && shadow_pos.x > 0.0 && shadow_pos.x < 1.0 &&
                        shadow_pos.y > 0.0 && shadow_pos.y < 1.0 && shadow_pos.z > 0.0 && shadow_pos.z < 1.0 )
                    {
                        float_t bias = u_shadow_settings.y + u_shadow_settings.z * ( 1.0 - k ) ;
                        vec2_t texel = 1.0 / texture_dims( u_shadow_map ) ;
                        float_t blocked = 0.0 ;
                        for( int_t x = -1; x <= 1; ++x )
                        {
                            for( int_t y = -1; y <= 1; ++y )
                            {
                                vec2_t uv = shadow_pos.xy + vec2_t( x, y ) ' texel ;
                                float_t stored = rt_texture( u_shadow_map, uv ).r ;
                                blocked += shadow_pos.z - bias > stored ? 1.0 : 0.0 ;
                            }
                        }
                        visibility = 1.0 - blocked / 9.0 ;
                    }
                    float_t fill = max( dot( n, normalize( -u_direction_1 ) ), 0.0 ) ;
                    vec3_t energy = vec3_t( 0.10, 0.13, 0.18 ) + u_energy_0 * k * visibility + u_energy_1 * fill ;
                    vec3_t fire = u_fire.xyz - in.world_pos ;
                    float_t distance2 = max( dot( fire, fire ), 0.1 ) ;
                    float_t flicker = 0.94 + 0.06 * sin( u_clock.x * 8.0 ) ;
                    float_t falloff = clamp( 1.0 - distance2 / ( u_fire.w * u_fire.w ), 0.0, 1.0 ) ;
                    energy = energy + u_energy_2 * max( dot( n, normalize( fire ) ), 0.0 ) * ( falloff * flicker / ( 1.0 + distance2 ) ) ;
                    float_t specular = pow( max( dot( n, normalize( key + v ) ), 0.0 ), u_material.y ) * u_material.x * k ;
                    vec3_t rgb = in.color.xyz ' energy + u_energy_0 * specular * visibility + in.color.xyz * in.color.w * u_clock.y * u_material.z ;
                    float_t fog = clamp( ( length( u_eye - in.world_pos ) - 30.0 ) * 0.008, 0.0, 0.30 ) ;
                    rgb = rgb * ( 1.0 - fog ) + vec3_t( 0.10, 0.15, 0.22 ) * fog ;
                    out.color = vec4_t( rgb, 1.0 ) ;
                }
            }
        }
    )" ;
    }

    inline motor::string_t make_shadow_shader( void_t )
    {
        return motor::string_t("config village_moon_shadow {")+surface_vertex_shader+R"(
        pixel_shader
        {
            in vec4_t color : color ;
            in vec3_t world_pos : texcoord0 ;
            in vec3_t world_normal : texcoord1 ;
            in vec4_t light_pos : texcoord2 ;
            in float_t shadow_depth : texcoord3 ;
            out float_t depth : depth ;
            void main()
            {
                out.depth = in.shadow_depth ;
            }
        }
    })" ;
    }

    // Analytic particle motion: persistent seeds, GPU-generated positions, no CPU readback.
    inline char const * capture_shader = R"(
        config village_capture
        {
            vertex_shader
            {
                inout vec4_t pos : position ;
                inout vec4_t color : color ;
                vec4_t u_clock ;
                vec4_t u_dragon ;
                in vec4_t tile : texcoord0 ;
                void main()
                {
                    float_t seed = in.pos.w ;
                    float_t age = fract( seed + u_clock.x * 0.13 ) ;
                    vec3_t p = in.pos.xyz ;
                    float_t size = 0.10 + age * 0.50 ;
                    vec4_t tint = vec4_t( 0.42, 0.47, 0.53, 0.06 ) ;
                    p = p + vec3_t( age * 1.6 + sin( age * 10.0 + seed * 50.0 ) * age * 0.30, age * 5.0, sin( age * 8.0 + seed * 33.0 ) * age * 0.45 ) ;
                    tint.w = tint.w * sin( age * 3.14159265 ) ;
                    if( in.color.w > 0.5 )
                    {
                        age = fract( seed + u_clock.x * 0.32 ) ;
                        p = in.pos.xyz + vec3_t( sin( seed * 140.0 ) * age * 1.2, age * 3.2, cos( seed * 93.0 ) * age * 1.2 ) ;
                        size = 0.018 + in.color.x * 0.025 ;
                        tint = vec4_t( 4.0, 1.2, 0.08, ( 1.0 - age ) * 0.55 ) ;
                    }
                    if( in.color.w > 1.5 )
                    {
                        p = in.pos.xyz + vec3_t( sin( u_clock.x * 0.24 + seed * 17.0 ), sin( u_clock.x * 0.32 + seed * 91.0 ) * 0.45, cos( u_clock.x * 0.21 + seed * 43.0 ) ) ;
                        size = 0.012 + in.color.x * 0.020 ;
                        tint = vec4_t( 1.2, 1.0, 0.55, 0.20 ) ;
                    }
                    if( in.color.w > 2.5 )
                    {
                        age = fract( seed + u_clock.x * 0.9 ) ;
                        float_t spread = 0.08 + age * 0.85 ;
                        vec3_t local = vec3_t( sin( seed * 87.0 ) * spread, 0.1 - age * 0.9 + cos( seed * 53.0 ) * spread * 0.6, 2.8 + age * 6.0 ) ;
                        float_t a = u_dragon.w ;
                        p = in.pos.xyz + u_dragon.xyz + vec3_t( cos( a ) * local.x + sin( a ) * local.z, local.y, -sin( a ) * local.x + cos( a ) * local.z ) ;
                        size = 0.07 + age * 0.28 ;
                        tint = vec4_t( 6.0, 2.4 * ( 1.0 - age ) + 0.12, 0.04, ( 1.0 - age ) * 0.32 * u_clock.w ) ;
                    }
                    float_t tile_angle = in.tile.w ;
                    p = in.tile.xyz + vec3_t( cos( tile_angle ) * p.x + sin( tile_angle ) * p.z,
                        p.y, -sin( tile_angle ) * p.x + cos( tile_angle ) * p.z ) ;
                    out.pos = vec4_t( p, size ) ; out.color = tint ;
                }
            }
            geometry_shader
            {
                in points ; out points[ max_verts = 1 ] ;
                inout vec4_t pos : position ; inout vec4_t color : color ;
                void main()
                {
                    for( int i = 0 ; i < in.length() ; ++i )
                    {
                        if( in[i].color.w > 0.003 )
                        { out.pos = in[i].pos ; out.color = in[i].color ; emit_vertex() ; }
                    }
                    end_primitive() ;
                }
            }
        }
    )" ;

    inline char const * particle_shader = R"(
        config village_particles
        {
            vertex_shader
            {
                inout vec4_t pos : position ; inout vec4_t color : color ;
                void main() { out.pos = in.pos ; out.color = in.color ; }
            }
            geometry_shader
            {
                in points ; out triangles[ max_verts = 4 ] ;
                inout vec4_t pos : position ; inout vec4_t color : color ;
                out vec2_t uv : texcoord0 ;
                mat4_t u_view_projection ; vec3_t u_right ; vec3_t u_up ;
                void main()
                {
                    for( int i = 0 ; i < in.length() ; ++i )
                    {
                        vec3_t p = in[i].pos.xyz ;
                        vec3_t u = u_right * in[i].pos.w ; vec3_t v = u_up * in[i].pos.w ;
                        out.pos = u_view_projection * vec4_t( p - u - v, 1.0 ) ; out.color = in[i].color ; out.uv = vec2_t( -1.0, -1.0 ) ; emit_vertex() ;
                        out.pos = u_view_projection * vec4_t( p + u - v, 1.0 ) ; out.color = in[i].color ; out.uv = vec2_t( 1.0, -1.0 ) ; emit_vertex() ;
                        out.pos = u_view_projection * vec4_t( p - u + v, 1.0 ) ; out.color = in[i].color ; out.uv = vec2_t( -1.0, 1.0 ) ; emit_vertex() ;
                        out.pos = u_view_projection * vec4_t( p + u + v, 1.0 ) ; out.color = in[i].color ; out.uv = vec2_t( 1.0, 1.0 ) ; emit_vertex() ;
                        end_primitive() ;
                    }
                }
            }
            pixel_shader
            {
                inout vec4_t color : color ; in vec2_t uv : texcoord0 ;
                void main()
                {
                    float_t a = max( 1.0 - dot( in.uv, in.uv ), 0.0 ) ;
                    out.color = vec4_t( in.color.xyz, in.color.w * a * a ) ;
                }
            }
        }
    )" ;
}
