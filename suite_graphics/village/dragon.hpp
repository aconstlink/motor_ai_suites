#pragma once
#include "geometry.hpp"

namespace village
{
    // Negative color.w values mark procedural joints; positive values remain emission.
    inline void_t dragon_spike( mesh & m, vec3 const base, vec3 const tip,
        float_t const radius, vec4 const color )
    {
        auto const previous = m.surface ; m.surface = material_id::horn ;
        auto const axis = (tip-base).normalized() ;
        auto const helper = std::abs(axis.y()) > 0.9f ? vec3(1,0,0) : vec3(0,1,0) ;
        auto const u = vec3(helper).cross(axis).normalized()*radius ;
        auto const v = vec3(axis).cross(u).normalized()*radius ;
        for( size_t i = 0 ; i < 8 ; ++i )
        {
            float_t const a = 2*pi*float_t(i)/8, b = 2*pi*float_t(i+1)/8 ;
            auto const p = base+u*std::cos(a)+v*std::sin(a) ;
            auto const q = base+u*std::cos(b)+v*std::sin(b) ;
            m.triangle(p,q,tip,color) ;
        }
        m.surface = previous ;
    }

    inline mesh make_dragon( void_t )
    {
        mesh m ; m.surface = material_id::skin ;
        vec4 const skin(0.075f,0.26f,0.20f,0), dark(0.035f,0.12f,0.09f,0) ;
        vec4 const belly(0.48f,0.43f,0.20f,0), horn(0.75f,0.59f,0.31f,0) ;
        m.ellipsoid(vec3(0,0,0),vec3(0.75f,0.65f,1.55f),14,skin) ;
        m.ellipsoid(vec3(0,-0.35f,0.15f),vec3(0.57f,0.37f,1.23f),12,belly) ;
        m.ellipsoid(vec3(0,0.25f,1.35f),vec3(0.40f,0.53f,0.82f),12,skin) ;
        m.ellipsoid(vec3(0,0.48f,2.0f),vec3(0.49f,0.37f,0.68f),12,skin) ;
        m.ellipsoid(vec3(0,0.25f,2.48f),vec3(0.34f,0.17f,0.48f),10,dark) ;
        m.ellipsoid(vec3(0,-0.03f,2.43f),vec3(0.32f,0.10f,0.44f),10,skin) ;
        for( float_t const side : {-1.0f,1.0f} )
        {
            m.surface = material_id::glass ;
            m.ellipsoid(vec3(side*0.40f,0.58f,2.17f),vec3(0.12f,0.11f,0.15f),8,vec4(1,0.48f,0.035f,4)) ;
            m.surface = material_id::skin ;
            m.beam(vec3(side*0.30f,0.72f,1.95f),vec3(side*0.51f,0.69f,2.33f),0.11f,dark) ;
            dragon_spike(m,vec3(side*0.32f,0.74f,1.76f),vec3(side*0.75f,1.31f,1.02f),0.19f,horn) ;
            for( float_t const z : {2.25f,2.48f,2.67f} )
                dragon_spike(m,vec3(side*0.26f,0.17f,z),vec3(side*0.21f,-0.02f,z+0.03f),0.055f,horn) ;
            for( float_t const z : {-0.8f,0.85f} )
            {
                m.ellipsoid(vec3(side*0.61f,-0.4f,z),vec3(0.28f,0.36f,0.40f),10,skin) ;
                m.beam(vec3(side*0.67f,-0.4f,z),vec3(side*0.78f,-0.9f,z-0.2f),0.19f,skin) ;
                m.beam(vec3(side*0.78f,-0.9f,z-0.2f),vec3(side*0.73f,-1.12f,z+0.17f),0.14f,dark) ;
                for( int c = -1 ; c <= 1 ; ++c )
                    dragon_spike(m,vec3(side*0.73f+float_t(c)*0.09f,-1.1f,z+0.13f),
                        vec3(side*0.73f+float_t(c)*0.11f,-1.16f,z+0.42f),0.05f,horn) ;
            }
        }
        for( int i = 0 ; i < 9 ; ++i )
        {
            float_t const z = -1.2f+float_t(i)*0.35f ;
            dragon_spike(m,vec3(0,0.55f,z),vec3(0,0.95f,z-0.2f),0.14f,horn) ;
        }

        // Two rigid wing roots with triangulated, scalloped membranes and raised fingers.
        for( float_t const side : {-1.0f,1.0f} )
        {
            float_t const joint = side > 0 ? -1.0f : -2.0f ;
            vec4 const membrane(0.44f,0.11f,0.065f,joint), bone(0.11f,0.29f,0.20f,joint) ;
            vec3 const shoulder(side*0.65f,0.25f,0.40f), elbow(side*2.1f,0.48f,1.05f), wrist(side*3.2f,0.55f,1.3f) ;
            vec3 const edge[] = {vec3(side*5.4f,0.28f,0.1f),vec3(side*4.1f,0.05f,-0.25f),
                vec3(side*4.35f,-0.08f,-1.5f),vec3(side*3.0f,-0.1f,-1.13f),
                vec3(side*2.8f,-0.12f,-2.2f),vec3(side*1.45f,0,-1.45f),vec3(side*0.70f,0.15f,-1.0f)} ;
            auto membrane_face = [&]( vec3 const a, vec3 const b, vec3 const c, vec4 const color )
            {
                m.surface = material_id::membrane ;
                if( side > 0 ) m.triangle(a,b,c,color) ;
                else m.triangle(a,c,b,color) ;
                m.surface = material_id::skin ;
            } ;
            membrane_face(shoulder,elbow,wrist,membrane) ;
            membrane_face(shoulder,wrist,edge[6],membrane) ;
            for( size_t i = 0 ; i < 6 ; ++i )
            {
                auto tint = membrane ; float_t const shade = 0.78f+float_t(i%3)*0.13f ;
                tint.x()*=shade ; tint.y()*=shade ; tint.z()*=shade ;
                membrane_face(wrist,edge[i],edge[i+1],tint) ;
                m.beam(edge[i],edge[i+1],0.055f,bone) ;
            }
            m.beam(shoulder,elbow,0.24f,bone) ; m.beam(elbow,wrist,0.17f,bone) ;
            for( size_t const i : {size_t(0),size_t(2),size_t(4),size_t(6)} ) m.beam(wrist,edge[i],0.085f,bone) ;
            dragon_spike(m,wrist,wrist+vec3(side*0.15f,0.12f,0.45f),0.12f,vec4(0.75f,0.59f,0.31f,joint)) ;
        }

        // Continuous tapered tail. The shader bends the mesh and corrects its normals.
        for( size_t r = 0 ; r < 24 ; ++r )
        {
            float_t const a = float_t(r)/24, b = float_t(r+1)/24 ;
            for( size_t s = 0 ; s < 12 ; ++s )
            {
                float_t const p = 2*pi*float_t(s)/12, q = 2*pi*float_t(s+1)/12 ;
                auto point = []( float_t const t, float_t const angle )
                {
                    float_t const radius = 0.42f*(1-t)+0.025f ;
                    return vec3(std::cos(angle)*radius,-0.05f+std::sin(angle)*radius,-1.1f-t*3.9f) ;
                } ;
                m.quad(point(a,p),point(b,p),point(b,q),point(a,q),vec4(0.075f,0.26f,0.20f,-3)) ;
            }
            if( r%3 == 0 ) dragon_spike(m,vec3(0,0.4f*(1-a),-1.1f-a*3.9f),
                vec3(0,0.4f*(1-a)+0.30f*(1-a),-1.25f-a*3.9f),0.10f*(1-a)+0.02f,vec4(0.75f,0.59f,0.31f,-3)) ;
        }
        return m ;
    }
}
