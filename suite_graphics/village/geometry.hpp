#pragma once
#include <motor/math/vector/vector3.hpp>
#include <motor/math/vector/vector4.hpp>
#include <motor/std/vector>
#include <motor/std/string>
#include <cmath>
#include <algorithm>

namespace village
{
    using namespace motor::core::types ;
    using vec3 = motor::math::vec3f_t ;
    using vec4 = motor::math::vec4f_t ;
    constexpr float_t pi = 3.14159265359f ;
    struct vertex { vec3 position, normal ; vec4 color ; } ;

    enum class material_id : size_t
    { plaster, wood, roof, stone, metal, glass, foliage, water, skin, membrane, horn, count } ;
    struct material
    {
        char const * name ;
        // Blinn-Phong specular weight, exponent, emission multiplier; not a PBR model.
        vec3 response ;
    } ;
    inline material const materials[] = {
        {"Plaster",vec3(0.015f,8,1)}, {"Timber",vec3(0.045f,24,1)},
        {"Roof / brick",vec3(0.06f,32,1)}, {"Stone",vec3(0.025f,12,1)},
        {"Iron / brass",vec3(0.55f,80,1)}, {"Emissive glass / fire",vec3(0.25f,96,1)},
        {"Foliage",vec3(0.025f,16,1)}, {"Water",vec3(0.65f,128,1)},
        {"Dragon scales",vec3(0.18f,48,1)}, {"Wing membrane",vec3(0.04f,16,1)},
        {"Horn / claws",vec3(0.22f,64,1)}
    } ;
    static_assert(sizeof(materials)/sizeof(materials[0]) == size_t(material_id::count)) ;

    struct mesh
    {
        motor::vector<vertex> vertices ;
        motor::vector<uint_t> indices ;
        motor::vector<material_id> triangle_materials ;
        material_id surface = material_id::wood ;
        vec3 origin = vec3(0) ;
        float_t angle = 0 ;
        uint32_t random_state = 1800 ;

        float_t random( void_t )
        { random_state = random_state * 1664525u + 1013904223u ; return float_t(random_state >> 8) / 16777216.0f ; }
        vec3 rotate( vec3 const p ) const
        { return vec3(std::cos(angle)*p.x()+std::sin(angle)*p.z(),p.y(),-std::sin(angle)*p.x()+std::cos(angle)*p.z()) ; }
        vec3 world( vec3 const p ) const { return rotate(p)+origin ; }
        void_t quad( vec3 const a, vec3 const b, vec3 const c, vec3 const d, vec4 const color )
        {
            auto const normal = rotate((b-a).cross(c-a).normalized()) ;
            auto const offset = uint_t(vertices.size()) ;
            for( auto const p : {a,b,c,d} ) vertices.push_back({world(p),normal,color}) ;
            for( uint_t const i : {0u,1u,2u,0u,2u,3u} ) indices.push_back(offset+i) ;
            triangle_materials.push_back(surface) ; triangle_materials.push_back(surface) ;
        }
        void_t triangle( vec3 const a, vec3 const b, vec3 const c, vec4 const color )
        {
            auto const normal = rotate((b-a).cross(c-a).normalized()) ;
            auto const offset = uint_t(vertices.size()) ;
            for( auto const p : {a,b,c} ) vertices.push_back({world(p),normal,color}) ;
            for( uint_t i = 0 ; i < 3 ; ++i ) indices.push_back(offset+i) ;
            triangle_materials.push_back(surface) ;
        }
        void_t prism( vec3 const c, vec3 const x, vec3 const y, vec3 z, vec4 const color )
        {
            // Mirrored roof slopes still need outward-facing normals.
            if( vec3(x).cross(y).dot(z) < 0 ) z = -z ;
            quad(c-x-y+z,c+x-y+z,c+x+y+z,c-x+y+z,color) ;
            quad(c+x-y-z,c-x-y-z,c-x+y-z,c+x+y-z,color) ;
            quad(c+x-y+z,c+x-y-z,c+x+y-z,c+x+y+z,color) ;
            quad(c-x-y-z,c-x-y+z,c-x+y+z,c-x+y-z,color) ;
            quad(c-x+y+z,c+x+y+z,c+x+y-z,c-x+y-z,color) ;
            quad(c-x-y-z,c+x-y-z,c+x-y+z,c-x-y+z,color) ;
        }
        void_t box( vec3 const c, vec3 const size, vec4 const color )
        { prism(c,vec3(size.x()*0.5f,0,0),vec3(0,size.y()*0.5f,0),vec3(0,0,size.z()*0.5f),color) ; }

        void_t panel( motor::vector<vec3> outline, vec3 const depth, vec4 const color )
        {
            // Convex outline, extruded symmetrically. Keep both caps and all edges outward.
            if( (outline[1]-outline[0]).cross(outline[2]-outline[0]).dot(depth) < 0 )
                std::reverse(outline.begin(),outline.end()) ;
            vec3 center(0) ; for( auto const & p : outline ) center += p ;
            center *= 1.0f/float_t(outline.size()) ;
            for( size_t i = 0 ; i < outline.size() ; ++i )
            {
                auto const a = outline[i], b = outline[(i+1)%outline.size()] ;
                triangle(center+depth,a+depth,b+depth,color) ;
                triangle(center-depth,b-depth,a-depth,color) ;
                quad(a-depth,b-depth,b+depth,a+depth,color) ;
            }
        }

        void_t roof_tile( vec3 const center, vec3 const across, vec3 const down,
            float_t const thickness, size_t const segments, vec4 const color )
        {
            auto const u = across.normalized(), v = down.normalized() ;
            float_t const w = across.length(), h = down.length(), round = h*0.60f ;
            auto n = vec3(u).cross(v).normalized() ; if( n.y() < 0 ) n = -n ;
            motor::vector<vec3> outline ; outline.reserve(segments+3) ;
            outline.push_back(center-u*w-v*h) ; outline.push_back(center+u*w-v*h) ;
            for( size_t i = 0 ; i <= segments ; ++i )
            {
                float_t const a = pi*float_t(i)/float_t(segments) ;
                outline.push_back(center+u*(w*std::cos(a))+v*(h-round+round*std::sin(a))) ;
            }
            panel(std::move(outline),n*(thickness*0.5f),color) ;
        }

        void_t ring( vec3 const center, vec3 const axis, float_t const radius,
            float_t const tube, size_t const segments, vec4 const color )
        {
            auto const n = axis.normalized() ;
            auto const helper = std::abs(n.y()) > 0.9f ? vec3(1,0,0) : vec3(0,1,0) ;
            auto const u = vec3(helper).cross(n).normalized(), v = vec3(n).cross(u) ;
            auto point = [&]( size_t const i, size_t const j )
            {
                float_t const a = 2*pi*float_t(i)/float_t(segments), b = 2*pi*float_t(j)/6 ;
                return center+(u*std::cos(a)+v*std::sin(a))*(radius+tube*std::cos(b))+n*(tube*std::sin(b)) ;
            } ;
            for( size_t i = 0 ; i < segments ; ++i ) for( size_t j = 0 ; j < 6 ; ++j )
                quad(point(i,j),point(i+1,j),point(i+1,j+1),point(i,j+1),color) ;
        }

        void_t ridge_cap( vec3 const center, float_t const radius, float_t const half_length,
            size_t const segments, vec4 const color )
        {
            // Half-round roof cap, with a visible inner shell and closed end rims.
            auto point = [&]( size_t const i, float_t const r, float_t const z )
            {
                float_t const a = pi*float_t(i)/float_t(segments) ;
                return center+vec3(r*std::cos(a),r*std::sin(a),z) ;
            } ;
            float_t const inner = radius*0.72f ;
            for( size_t i = 0 ; i < segments ; ++i )
            {
                auto const a = point(i,radius,-half_length), b = point(i+1,radius,-half_length) ;
                auto const c = point(i+1,radius,half_length), d = point(i,radius,half_length) ;
                auto const e = point(i,inner,-half_length), f = point(i+1,inner,-half_length) ;
                auto const g = point(i+1,inner,half_length), h = point(i,inner,half_length) ;
                quad(a,b,c,d,color) ; quad(e,h,g,f,color) ;
                quad(a,e,f,b,color) ; quad(d,c,g,h,color) ;
            }
            for( size_t const i : {size_t(0),segments} )
            {
                auto const a = point(i,radius,-half_length), b = point(i,inner,-half_length) ;
                auto const c = point(i,inner,half_length), d = point(i,radius,half_length) ;
                if( i == 0 ) quad(a,d,c,b,color) ; else quad(a,b,c,d,color) ;
            }
        }
        void_t beam( vec3 const a, vec3 const b, float_t const width, vec4 const color )
        {
            auto const y = (b-a)*0.5f ;
            auto const n = y.normalized() ;
            auto const helper = std::abs(n.y()) > 0.9f ? vec3(1,0,0) : vec3(0,1,0) ;
            auto const x = vec3(helper).cross(n).normalized() * (width*0.5f) ;
            auto const z = x.normalized().cross(n) * (width*0.5f) ;
            prism((a+b)*0.5f,x,y,z,color) ;
        }
        void_t stone( vec3 const c, vec3 const size, vec4 const color )
        {
            vec3 const x(size.x()*0.5f,0,0), z(0,0,size.z()*0.5f), y(0,size.y()*0.5f,0) ;
            quad(c-x*0.86f+y+z*0.86f,c+x*0.86f+y+z*0.86f,c+x*0.86f+y-z*0.86f,c-x*0.86f+y-z*0.86f,color) ;
            vec3 bottom[4] = {c-x+z,c+x+z,c+x-z,c-x-z} ;
            vec3 top[4] = {c-x*0.86f+y+z*0.86f,c+x*0.86f+y+z*0.86f,c+x*0.86f+y-z*0.86f,c-x*0.86f+y-z*0.86f} ;
            for( size_t i = 0 ; i < 4 ; ++i )
            {
                auto const j = (i+1)%4 ;
                quad(bottom[i],bottom[j],top[j],top[i],color) ;
                quad(bottom[i]-y,bottom[j]-y,bottom[j],bottom[i],color) ;
            }
        }
        void_t cylinder( vec3 const c, float_t const radius, float_t const height, size_t const sides, vec4 const color )
        {
            for( size_t i = 0 ; i < sides ; ++i )
            {
                float_t const a = 2*pi*float_t(i)/float_t(sides), b = 2*pi*float_t(i+1)/float_t(sides) ;
                vec3 const p(radius*std::cos(a),0,radius*std::sin(a)), q(radius*std::cos(b),0,radius*std::sin(b)), y(0,height,0) ;
                quad(c+q,c+p,c+p+y,c+q+y,color) ;
                triangle(c+y,c+q+y,c+p+y,color) ;
            }
        }
        void_t ellipsoid( vec3 const c, vec3 const scale, size_t const rings, vec4 const color )
        {
            auto point = [&]( size_t const r, size_t const s )
            {
                float_t const a = pi*float_t(r)/float_t(rings), b = pi*float_t(s)/float_t(rings) ;
                return c + vec3(scale.x()*std::sin(a)*std::cos(b),scale.y()*std::cos(a),scale.z()*std::sin(a)*std::sin(b)) ;
            } ;
            for( size_t r = 0 ; r < rings ; ++r ) for( size_t s = 0 ; s < rings*2 ; ++s )
            {
                auto tint = color ; float_t const shade = 0.85f+random()*0.3f ;
                tint.x() *= shade ; tint.y() *= shade ; tint.z() *= shade ;
                if( r == 0 ) triangle(point(r,s),point(r+1,s+1),point(r+1,s),tint) ;
                else if( r+1 == rings ) triangle(point(r,s),point(r,s+1),point(r+1,s),tint) ;
                else quad(point(r,s),point(r,s+1),point(r+1,s+1),point(r+1,s),tint) ;
            }
        }
    } ;

    // Preserve the original vertices/joint tags while making one draw mesh per material.
    inline mesh material_mesh( mesh const & source, material_id const id )
    {
        mesh result ; result.surface = id ;
        motor::vector<uint_t> remap(source.vertices.size(),uint_t(-1)) ;
        for( size_t t = 0 ; t < source.triangle_materials.size() ; ++t )
        {
            if( source.triangle_materials[t] != id ) continue ;
            for( size_t c = 0 ; c < 3 ; ++c )
            {
                uint_t const index = source.indices[t*3+c] ;
                if( remap[index] == uint_t(-1) )
                {
                    remap[index] = uint_t(result.vertices.size()) ;
                    result.vertices.push_back(source.vertices[index]) ;
                }
                result.indices.push_back(remap[index]) ;
            }
            result.triangle_materials.push_back(id) ;
        }
        return result ;
    }

    inline void_t window( mesh & m, vec3 const center, float_t const w, float_t const h, vec4 const shutter )
    {
        vec4 const wood(0.10f,0.067f,0.043f,0), glow(1.0f,0.53f,0.16f,3.0f) ;
        auto const previous = m.surface ; m.surface = material_id::wood ;
        m.box(center,vec3(w,h,0.09f),wood) ;
        m.surface = material_id::glass ;
        m.box(center+vec3(0,0,0.06f),vec3(w-0.13f,h-0.13f,0.04f),glow) ;
        m.surface = material_id::wood ;
        m.box(center+vec3(0,0,0.10f),vec3(0.055f,h,0.055f),wood) ;
        m.box(center+vec3(0,0,0.10f),vec3(w,0.055f,0.055f),wood) ;
        // Raised inner/outer mouldings and small leaded panes catch different highlights.
        for( float_t const side : {-1.0f,1.0f} )
        {
            m.box(center+vec3(side*(w*0.5f-0.025f),0,0.085f),vec3(0.065f,h+0.055f,0.075f),wood) ;
            m.box(center+vec3(0,side*(h*0.5f-0.025f),0.085f),vec3(w+0.055f,0.065f,0.075f),wood) ;
        }
        m.surface = material_id::metal ;
        for( float_t const x : {-1.0f,1.0f} ) for( float_t const y : {-1.0f,1.0f} )
        {
            auto const p = center+vec3(x*w*0.23f,y*h*0.23f,0.092f) ;
            m.beam(p+vec3(-w*0.16f,-h*0.16f,0),p+vec3(w*0.16f,h*0.16f,0),0.012f,vec4(0.12f,0.14f,0.14f,0)) ;
            m.beam(p+vec3(-w*0.16f,h*0.16f,0),p+vec3(w*0.16f,-h*0.16f,0),0.012f,vec4(0.12f,0.14f,0.14f,0)) ;
        }
        m.surface = material_id::wood ;
        for( float_t const side : {-1.0f,1.0f} )
        {
            auto const p = center+vec3(side*(w*0.5f+0.25f),0,0.025f) ;
            m.box(p,vec3(0.43f,h+0.08f,0.10f),shutter) ;
            for( int i = 0 ; i < 3 ; ++i ) m.box(p+vec3(float_t(i-1)*0.12f,0,0.06f),vec3(0.018f,h,0.03f),wood) ;
            for( float_t const y : {-0.32f,0.32f} ) m.box(p+vec3(0,y,0.09f),vec3(0.43f,0.045f,0.035f),wood) ;
            m.surface = material_id::metal ;
            for( float_t const y : {-h*0.32f,h*0.32f} )
            {
                m.box(p+vec3(0,y,0.115f),vec3(0.30f,0.04f,0.025f),vec4(0.11f,0.13f,0.14f,0)) ;
                for( float_t const x : {-0.10f,0.10f} )
                    m.box(p+vec3(x,y,0.133f),vec3(0.025f,0.025f,0.02f),vec4(0.24f,0.25f,0.25f,0)) ;
            }
            m.surface = material_id::wood ;
        }
        m.surface = material_id::stone ;
        m.box(center+vec3(0,-h*0.5f-0.08f,0.06f),vec3(w+0.25f,0.13f,0.32f),vec4(0.30f,0.29f,0.26f,0)) ;
        m.surface = previous ;
    }

    inline void_t house_details( mesh & m, float_t const height, size_t const index, vec4 const shutter )
    {
        vec4 const timber(0.18f,0.105f,0.055f,0), iron(0.10f,0.12f,0.13f,0) ;
        auto const origin = m.origin ; float_t const angle = m.angle ;
        // Each facade gets its own temporary local frame; the house frame is restored below.
        for( size_t face = 0 ; face < 4 ; ++face )
        {
            m.angle = angle+float_t(face)*pi*0.5f ;
            float_t const z = face%2 == 0 ? 2.76f : 2.66f ;
            if( face != 0 ) for( float_t const x : {-1.15f,1.15f} )
            {
                window(m,vec3(x,3.6f,z),0.75f,1.0f,shutter) ;
                window(m,vec3(x,1.65f,z),0.68f,0.90f,shutter) ;
            }
            m.surface = material_id::wood ;
            for( float_t const x : {-2.0f,-1.0f,0.0f,1.0f,2.0f} )
            {
                m.beam(vec3(x,height-0.40f,z-0.04f),vec3(x,height-0.03f,z+0.22f),0.12f,timber) ;
                m.box(vec3(x,height,z+0.13f),vec3(0.15f,0.16f,0.52f),timber) ;
            }
            for( float_t const x : {-1.15f,1.15f} )
            {
                auto const p = vec3(x,2.92f,z+0.20f) ;
                m.surface = material_id::wood ; m.box(p,vec3(1.0f,0.24f,0.34f),timber) ;
                m.surface = material_id::metal ;
                for( float_t const dx : {-0.43f,0.43f} )
                    m.box(p+vec3(dx,0,0.18f),vec3(0.045f,0.28f,0.035f),iron) ;
                m.surface = material_id::foliage ;
                for( int i = 0 ; i < 5 ; ++i )
                {
                    auto const flower = p+vec3(-0.36f+float_t(i)*0.18f,0.20f+0.035f*float_t(i%2),0) ;
                    m.ellipsoid(flower,vec3(0.13f,0.11f,0.14f),3,vec4(0.10f,0.25f,0.06f,0)) ;
                    m.ellipsoid(flower+vec3(0,0.10f,0.02f),vec3(0.055f),3,
                        index%2 == 0 ? vec4(0.72f,0.09f,0.12f,0) : vec4(0.75f,0.57f,0.10f,0)) ;
                }
            }
        }
        m.angle = angle ;
        m.surface = material_id::wood ;
        for( float_t const x : {0.43f,1.57f} ) m.box(vec3(x,1.40f,2.88f),vec3(0.16f,2.22f,0.20f),timber) ;
        m.box(vec3(1,2.52f,2.88f),vec3(1.30f,0.18f,0.20f),timber) ;
        for( float_t const x : {0.76f,1.22f} ) for( float_t const y : {0.88f,1.70f} )
        {
            auto const p = vec3(x,y,2.88f) ;
            for( float_t const s : {-1.0f,1.0f} )
            {
                m.box(p+vec3(s*0.17f,0,0),vec3(0.045f,0.63f,0.06f),timber) ;
                m.box(p+vec3(0,s*0.30f,0),vec3(0.38f,0.045f,0.06f),timber) ;
            }
        }
        m.surface = material_id::metal ;
        m.ellipsoid(vec3(1,1.40f,2.94f),vec3(0.09f,0.13f,0.025f),5,vec4(0.46f,0.30f,0.08f,0)) ;
        m.ring(vec3(1,1.30f,2.98f),vec3(0,0,1),0.085f,0.015f,16,iron) ;
        m.surface = material_id::wood ;
        for( float_t const x : {0.28f,1.72f} )
        {
            m.beam(vec3(x,2.23f,2.85f),vec3(x,2.84f,3.70f),0.11f,timber) ;
            m.beam(vec3(x,2.86f,2.76f),vec3(x,2.86f,3.86f),0.12f,timber) ;
        }
        m.surface = material_id::roof ;
        for( float_t const side : {-1.0f,1.0f} )
            m.prism(vec3(1+side*0.46f,3.03f,3.28f),vec3(0.49f,-side*0.20f,0),vec3(0,0.055f,0),vec3(0,0,0.70f),vec4(0.27f,0.12f,0.07f,0)) ;
        m.surface = material_id::metal ;
        for( float_t const y : {0.72f,1.95f} )
        {
            m.box(vec3(0.88f,y,2.89f),vec3(0.65f,0.075f,0.035f),iron) ;
            for( float_t const x : {0.62f,0.86f,1.10f} ) m.box(vec3(x,y,2.915f),vec3(0.035f,0.035f,0.025f),iron) ;
        }
        // Emissive lantern, without adding another analytical light or shadow pass.
        m.beam(vec3(2.06f,2.60f,2.78f),vec3(2.06f,2.60f,3.12f),0.05f,iron) ;
        m.beam(vec3(2.06f,2.60f,3.12f),vec3(2.06f,2.38f,3.12f),0.04f,iron) ;
        for( float_t const y : {1.96f,2.40f} ) m.box(vec3(2.06f,y,3.12f),vec3(0.35f,0.06f,0.29f),iron) ;
        for( float_t const x : {-0.14f,0.14f} ) for( float_t const z : {-0.11f,0.11f} )
            m.box(vec3(2.06f+x,2.18f,3.12f+z),vec3(0.035f,0.44f,0.035f),iron) ;
        m.surface = material_id::glass ;
        m.box(vec3(2.06f,2.18f,3.12f),vec3(0.24f,0.35f,0.18f),vec4(1,0.52f,0.16f,2.0f)) ;
        // Hanging trade sign, with a different geometric emblem on each house.
        m.surface = material_id::metal ;
        m.beam(vec3(-2.0f,2.80f,2.72f),vec3(-2.0f,2.80f,3.75f),0.055f,iron) ;
        m.beam(vec3(-2.0f,3.12f,2.72f),vec3(-2.0f,2.80f,3.45f),0.045f,iron) ;
        m.beam(vec3(-2.28f,2.80f,3.60f),vec3(-1.72f,2.80f,3.60f),0.045f,iron) ;
        for( float_t const x : {-2.22f,-1.78f} ) m.beam(vec3(x,2.80f,3.60f),vec3(x,2.62f,3.60f),0.025f,iron) ;
        m.surface = material_id::wood ;
        m.box(vec3(-2.0f,2.33f,3.60f),vec3(0.75f,0.68f,0.09f),timber) ;
        m.surface = material_id::metal ;
        float_t const emblem = float_t(index)*pi/8 ;
        m.beam(vec3(-2.0f-0.22f*std::cos(emblem),2.33f-0.22f*std::sin(emblem),3.66f),
            vec3(-2.0f+0.22f*std::cos(emblem),2.33f+0.22f*std::sin(emblem),3.66f),0.065f,vec4(0.68f,0.49f,0.19f,0)) ;
        // Dormers on both roof slopes break up the silhouette.
        for( float_t const side : {-1.0f,1.0f} )
        {
            m.origin = origin ; m.angle = angle ;
            m.origin = m.world(vec3(side*1.8f,height+1.18f,index%2 == 0 ? -0.65f : 0.65f)) ;
            m.angle = angle+side*pi*0.5f ;
            m.surface = material_id::plaster ;
            m.box(vec3(0,0.20f,-0.10f),vec3(1.0f,1.1f,0.95f),vec4(0.66f,0.64f,0.54f,0)) ;
            m.triangle(vec3(-0.5f,0.75f,0.39f),vec3(0.5f,0.75f,0.39f),vec3(0,1.10f,0.39f),vec4(0.66f,0.64f,0.54f,0)) ;
            window(m,vec3(0,0.30f,0.42f),0.52f,0.64f,shutter) ;
            m.surface = material_id::wood ;
            for( float_t const x : {-0.49f,0.49f} ) m.box(vec3(x,0.2f,0.43f),vec3(0.09f,1.12f,0.10f),timber) ;
            m.surface = material_id::roof ;
            for( float_t const s : {-1.0f,1.0f} ) for( int r = 0 ; r < 4 ; ++r ) for( int c = 0 ; c < 6 ; ++c )
            {
                float_t const t = (float_t(r)+0.5f)/4 ;
                m.roof_tile(vec3(s*t*0.65f,1.12f-t*0.43f,-0.65f+(float_t(c)+0.5f)*0.22f),
                    vec3(0,0,0.105f),vec3(s*0.087f,-0.058f,0),0.055f,5,vec4(0.30f,0.14f,0.08f,0)) ;
            }
        }
        m.origin = origin ; m.angle = angle ; m.surface = material_id::wood ;
    }

    inline vec3 house( mesh & m, size_t const index, int const detail )
    {
        vec4 const wood(0.12f,0.077f,0.047f,0), plaster(0.72f,0.70f,0.59f,0) ;
        vec4 const roof = index%2 == 0 ? vec4(0.30f,0.115f,0.067f,0) : vec4(0.13f,0.18f,0.21f,0) ;
        vec4 const shutter = index%2 == 0 ? vec4(0.08f,0.22f,0.19f,0) : vec4(0.25f,0.095f,0.065f,0) ;
        float_t const height = 4.8f + float_t(index%2)*0.45f, peak = height+2.6f ;
        m.surface = material_id::plaster ;
        m.box(vec3(0,height*0.5f,0),vec3(5.0f,height,5.2f),plaster) ;
        m.surface = material_id::stone ;
        for( int row = 0 ; row < 3 ; ++row ) for( int x = 0 ; x < 10 ; ++x )
        {
            float_t const px = -2.25f+float_t(x)*0.5f ;
            for( float_t const z : {-2.65f,2.65f} )
                m.stone(vec3(px,0.12f+float_t(row)*0.22f,z),vec3(0.48f,0.23f,0.20f),vec4(0.27f,0.29f,0.29f,0)) ;
        }
        for( float_t const z : {-2.67f,2.67f} )
        {
            m.surface = material_id::wood ;
            for( float_t const y : {0.70f,2.5f,height} ) m.box(vec3(0,y,z),vec3(5.2f,0.18f,0.18f),wood) ;
            for( float_t const x : {-2.42f,0.0f,2.42f} ) m.box(vec3(x,height*0.5f,z),vec3(0.18f,height,0.18f),wood) ;
            m.surface = material_id::plaster ;
            if( z > 0 ) m.triangle(vec3(-2.5f,height,z),vec3(2.5f,height,z),vec3(0,peak,z),plaster) ;
            else m.triangle(vec3(2.5f,height,z),vec3(-2.5f,height,z),vec3(0,peak,z),plaster) ;
            m.surface = material_id::wood ;
            m.beam(vec3(-2.6f,height,z),vec3(0,peak+0.08f,z),0.18f,wood) ;
            m.beam(vec3(2.6f,height,z),vec3(0,peak+0.08f,z),0.18f,wood) ;
            m.beam(vec3(0,height,z),vec3(0,peak,z),0.15f,wood) ;
            for( float_t const x : {-1.65f,-0.85f,0.85f,1.65f} )
            {
                float_t const y = peak-std::abs(x)*1.04f ;
                m.beam(vec3(x,height,z),vec3(x,y,z),0.12f,wood) ;
            }
            for( float_t const sign : {-1.0f,1.0f} )
            {
                m.beam(vec3(sign*2.35f,2.6f,z+0.02f),vec3(sign*1.75f,height-0.1f,z+0.02f),0.13f,wood) ;
                m.beam(vec3(sign*2.35f,0.8f,z+0.02f),vec3(sign*1.65f,2.4f,z+0.02f),0.13f,wood) ;
            }
        }
        for( float_t const x : {-2.56f,2.56f} )
        {
            for( float_t const y : {0.7f,2.5f,height} ) m.box(vec3(x,y,0),vec3(0.18f,0.18f,5.4f),wood) ;
            for( float_t const z : {-2.4f,-0.8f,0.8f,2.4f} ) m.box(vec3(x,height*0.5f,z),vec3(0.18f,height,0.16f),wood) ;
            for( int k = 0 ; k < 3 ; ++k ) m.beam(vec3(x,2.6f,-2.3f+float_t(k)*1.6f),vec3(x,height-0.1f,-0.95f+float_t(k)*1.6f),0.13f,wood) ;
        }
        for( float_t const x : {-1.15f,1.15f} )
        {
            window(m,vec3(x,3.6f,2.75f),0.75f,1.0f,shutter) ;
            if( x < 0 ) window(m,vec3(x,1.7f,2.75f),0.7f,0.9f,shutter) ;
        }
        m.box(vec3(1.0f,1.37f,2.70f),vec3(1.0f,2.05f,0.12f),wood) ;
        for( int p = 0 ; p < 6 ; ++p ) m.box(vec3(0.59f+float_t(p)*0.16f,1.35f,2.80f),vec3(0.14f,1.9f,0.09f),vec4(0.25f,0.16f,0.085f,0)) ;
        m.surface = material_id::metal ;
        m.box(vec3(1.30f,1.3f,2.87f),vec3(0.09f,0.08f,0.06f),vec4(0.6f,0.41f,0.1f,0)) ;
        m.surface = material_id::stone ;
        for( int s = 0 ; s < 3 ; ++s ) m.box(vec3(1,0.12f+float_t(s)*0.10f,3.1f-float_t(s)*0.15f),vec3(1.35f,0.2f,0.65f),vec4(0.32f,0.33f,0.33f,0)) ;
        size_t const rows = size_t(12+detail*8), cols = size_t(16+detail*8) ;
        for( float_t const side : {-1.0f,1.0f} )
        {
            auto const a = vec3(0,peak+0.12f,0), b = vec3(side*2.95f,height-0.33f,0) ;
            auto const slope = b-a ;
            auto const u = vec3(0,0,1) ;
            m.surface = material_id::roof ;
            for( size_t r = 0 ; r < rows ; ++r ) for( size_t c = 0 ; c < cols ; ++c )
            {
                float_t const t = (float_t(r)+0.5f)/float_t(rows), z = -2.95f+(float_t(c)+0.5f)*5.9f/float_t(cols) ;
                vec4 tint = roof ; float_t const shade = 0.75f+m.random()*0.5f ;
                tint.x()*=shade ; tint.y()*=shade ; tint.z()*=shade ;
                m.roof_tile(a+slope*t+vec3(0,0.045f,z),u*(2.88f/float_t(cols)),
                    slope*(0.54f/float_t(rows)),0.075f,size_t(3+detail*2),tint) ;
            }
            m.surface = material_id::wood ;
            m.beam(vec3(side*2.94f,height-0.3f,-3.02f),vec3(side*2.94f,height-0.3f,3.02f),0.16f,wood) ;
        }
        m.surface = material_id::roof ;
        for( int i = 0 ; i < 20 ; ++i )
            m.ridge_cap(vec3(0,peak+0.12f,-2.91f+float_t(i)*0.306f),0.19f,0.159f,size_t(6+detail*2),roof) ;
        vec3 const chimney(0.85f,peak-0.1f,-0.9f) ;
        m.box(chimney,vec3(0.65f,1.7f,0.65f),vec4(0.30f,0.16f,0.11f,0)) ;
        m.surface = material_id::stone ;
        for( int r = 0 ; r < 7 ; ++r ) m.box(chimney+vec3(0,-0.75f+float_t(r)*0.25f,0),vec3(0.69f,0.035f,0.69f),vec4(0.13f,0.13f,0.12f,0)) ;
        m.box(chimney+vec3(0,0.84f,0),vec3(0.84f,0.16f,0.84f),vec4(0.24f,0.24f,0.23f,0)) ;
        house_details(m,height,index,shutter) ;
        return m.world(chimney+vec3(0,1,0)) ;
    }

    inline void_t barrel( mesh & m, vec3 const p, float_t const scale )
    {
        size_t const n = 18 ;
        m.surface = material_id::wood ;
        float_t const radii[] = {0.29f,0.34f,0.37f,0.34f,0.29f} ;
        for( size_t i = 0 ; i < n ; ++i )
        {
            float_t const a = 2*pi*(float_t(i)+0.025f)/float_t(n), b = 2*pi*(float_t(i+1)-0.025f)/float_t(n) ;
            float_t const shade = 0.85f+float_t(i%4)*0.1f ;
            auto point = [&]( size_t const r, float_t const angle )
            { return p+vec3(std::cos(angle)*radii[r],float_t(r)*0.22f,std::sin(angle)*radii[r])*scale ; } ;
            for( size_t r = 0 ; r < 4 ; ++r )
                m.quad(point(r,b),point(r,a),point(r+1,a),point(r+1,b),vec4(0.34f*shade,0.21f*shade,0.10f*shade,0)) ;
        }
        m.cylinder(p+vec3(0,0.84f*scale,0),0.285f*scale,0.025f*scale,n,vec4(0.28f,0.17f,0.08f,0)) ;
        for( int i = -2 ; i <= 2 ; ++i )
        {
            float_t const x = float_t(i)*0.09f, z = std::sqrt(0.28f*0.28f-x*x) ;
            m.beam(p+vec3(x,0.868f,-z)*scale,p+vec3(x,0.868f,z)*scale,0.008f*scale,vec4(0.10f,0.06f,0.03f,0)) ;
        }
        m.surface = material_id::metal ;
        for( float_t const y : {0.16f,0.72f} ) m.ring(p+vec3(0,y*scale,0),vec3(0,1,0),0.334f*scale,0.022f*scale,24,vec4(0.12f,0.14f,0.15f,0)) ;
        m.surface = material_id::wood ;
    }

    inline void_t surroundings( mesh & ground, mesh & props, mesh & plants, int const detail )
    {
        ground.surface = material_id::stone ; props.surface = material_id::stone ;
        ground.box(vec3(0,-0.28f,0),vec3(33,0.5f,29),vec4(0.13f,0.19f,0.10f,0)) ;
        float_t const step = detail == 0 ? 0.68f : detail == 1 ? 0.34f : 0.20f ;
        for( float_t z = -11 ; z < 11 ; z += step ) for( float_t x = -13 ; x < 13 ; x += step )
        {
            float_t const shade = 0.82f+ground.random()*0.36f ;
            ground.stone(vec3(x+(int((z+11)/step)%2)*step*0.48f,0.03f+ground.random()*0.025f,z),
                vec3(step*0.94f,0.10f,step*0.91f),vec4(0.25f*shade,0.27f*shade,0.26f*shade,0)) ;
        }
        for( int layer = 0 ; layer < 4 ; ++layer ) for( int i = 0 ; i < 24 ; ++i )
        {
            float_t const a = 2*pi*(float_t(i)+(layer%2)*0.5f+0.025f)/24.0f ;
            float_t const b = a+2*pi*0.95f/24 ;
            float_t const y = 0.2f+float_t(layer)*0.25f ;
            props.panel({vec3(0.88f*std::cos(a),y,0.88f*std::sin(a)),vec3(1.23f*std::cos(a),y,1.23f*std::sin(a)),
                vec3(1.23f*std::cos(b),y,1.23f*std::sin(b)),vec3(0.88f*std::cos(b),y,0.88f*std::sin(b))},
                vec3(0,0.12f,0),vec4(0.38f,0.40f,0.39f,0)) ;
        }
        props.surface = material_id::water ;
        props.cylinder(vec3(0,0.38f,0),0.85f,0.02f,48,vec4(0.025f,0.16f,0.19f,0.15f)) ;
        props.surface = material_id::wood ;
        for( float_t const x : {-1.35f,1.35f} ) props.beam(vec3(x,0.1f,0),vec3(x,3.1f,0),0.17f,vec4(0.19f,0.12f,0.065f,0)) ;
        props.beam(vec3(-1.5f,3.05f,0),vec3(1.5f,3.05f,0),0.18f,vec4(0.19f,0.12f,0.065f,0)) ;
        props.ellipsoid(vec3(0,2.22f,0),vec3(1.50f,0.12f,0.12f),8,vec4(0.25f,0.14f,0.065f,0)) ;
        for( int i = 0 ; i < 18 ; ++i )
            props.ring(vec3(-0.24f+float_t(i)*0.028f,2.22f,0),vec3(1,0,0),0.13f,0.014f,16,vec4(0.40f,0.32f,0.18f,0)) ;
        props.beam(vec3(0,2.22f,0.14f),vec3(0,0.70f,0.14f),0.028f,vec4(0.40f,0.32f,0.18f,0)) ;
        props.surface = material_id::metal ;
        props.beam(vec3(1.48f,2.22f,0),vec3(1.48f,1.88f,0),0.055f,vec4(0.12f,0.14f,0.15f,0)) ;
        props.beam(vec3(1.48f,1.88f,0),vec3(1.75f,1.88f,0),0.075f,vec4(0.12f,0.14f,0.15f,0)) ;
        props.surface = material_id::roof ;
        for( float_t const side : {-1.0f,1.0f} ) props.prism(vec3(side*0.7f,3.3f,0),vec3(0.75f,-side*0.35f,0),vec3(0,0.06f,0),vec3(0,0,0.95f),vec4(0.25f,0.095f,0.045f,0)) ;
        props.surface = material_id::wood ;
        for( auto const p : {vec3(-3.6f,0.1f,-2.8f),vec3(-4.3f,0.1f,-2.5f),vec3(4,0.1f,-2),vec3(4.7f,0.1f,-2.1f)} ) barrel(props,p,0.9f+props.random()*0.4f) ;
        // Cart, planks, axle and spoked wheels.
        vec3 const cart(-3.5f,0.6f,4.2f) ;
        for( int i = 0 ; i < 7 ; ++i ) props.box(cart+vec3(float_t(i-3)*0.20f,0,0),vec3(0.18f,0.14f,2.0f),vec4(0.32f,0.22f,0.12f,0)) ;
        for( float_t const side : {-1.0f,1.0f} )
        {
            for( float_t const y : {0.25f,0.55f} ) props.box(cart+vec3(side*0.75f,y,0),vec3(0.1f,0.15f,2.05f),vec4(0.29f,0.17f,0.08f,0)) ;
            vec3 const c = cart+vec3(side*0.95f,-0.1f,0) ;
            props.surface = material_id::metal ;
            props.ring(c,vec3(1,0,0),0.565f,0.035f,32,vec4(0.14f,0.16f,0.17f,0)) ;
            props.surface = material_id::wood ;
            props.ellipsoid(c,vec3(0.17f,0.13f,0.13f),6,vec4(0.27f,0.16f,0.08f,0)) ;
            for( int i = 0 ; i < 24 ; ++i )
            {
                float_t const a=2*pi*float_t(i)/24, b=2*pi*float_t(i+1)/24 ;
                vec3 const p=c+vec3(0,std::cos(a)*0.55f,std::sin(a)*0.55f),q=c+vec3(0,std::cos(b)*0.55f,std::sin(b)*0.55f) ;
                props.beam(p,q,0.08f,vec4(0.15f,0.12f,0.08f,0)) ;
                if( i%3 == 0 ) props.beam(c,p,0.045f,vec4(0.42f,0.28f,0.12f,0)) ;
            }
            props.beam(cart+vec3(side*0.5f,0,0.8f),cart+vec3(side*0.5f,-0.3f,3.4f),0.085f,vec4(0.29f,0.17f,0.08f,0)) ;
        }
        for( int i = 0 ; i < 20 ; ++i )
        {
            vec3 const p(3.4f+float_t(i%5)*0.23f,0.23f+float_t(i/5)*0.22f,5.4f) ;
            props.beam(p,p+vec3(0,0,1.0f),0.20f,vec4(0.27f,0.16f,0.075f,0)) ;
        }
        for( float_t const x : {-12.0f,12.0f} ) for( float_t z = -10 ; z <= 10 ; z += 1.5f )
        {
            props.box(vec3(x,0.65f,z),vec3(0.12f,1.2f,0.12f),vec4(0.23f,0.19f,0.12f,0)) ;
            for( float_t const y : {0.45f,0.95f} ) props.box(vec3(x,y,z+0.7f),vec3(0.09f,0.10f,1.5f),vec4(0.30f,0.24f,0.14f,0)) ;
        }
        // Fire bowl supplies a warm local light and emissive source.
        props.surface = material_id::metal ;
        props.cylinder(vec3(2.8f,0.1f,2.6f),0.60f,0.35f,24,vec4(0.10f,0.12f,0.13f,0)) ;
        props.surface = material_id::glass ;
        props.ellipsoid(vec3(2.8f,0.54f,2.6f),vec3(0.38f,0.35f,0.38f),8,vec4(1.0f,0.24f,0.025f,7.0f)) ;
        for( auto const p : {vec3(-13,0,-10),vec3(12,0,-10),vec3(-13,0,8),vec3(13,0,9),vec3(0,0,-12)} )
        {
            plants.surface = material_id::wood ;
            plants.beam(p,p+vec3(0.35f,4.4f,0),0.45f,vec4(0.19f,0.14f,0.09f,0)) ;
            for( int i=0 ; i<12 ; ++i )
            {
                float_t const a=2*pi*plants.random() ;
                auto const end=p+vec3(std::cos(a)*1.4f,3.8f+plants.random()*2.0f,std::sin(a)*1.4f) ;
                plants.surface = material_id::wood ;
                plants.beam(p+vec3(0.2f,2.5f,0),end,0.13f,vec4(0.21f,0.15f,0.08f,0)) ;
                plants.surface = material_id::foliage ;
                plants.ellipsoid(end,vec3(1.1f,1.15f,1.0f),size_t(5+detail*3),vec4(0.12f,0.28f,0.065f,0)) ;
            }
        }
        size_t const blades = size_t(3000+detail*9000) ;
        plants.surface = material_id::foliage ;
        for( size_t i = 0 ; i < blades ; ++i )
        {
            float_t const x = -16+plants.random()*32, z = -14+plants.random()*28 ;
            if( std::abs(x)<13.5f && std::abs(z)<11.5f ) continue ;
            vec3 const p(x,0,z) ; float_t const h=0.12f+plants.random()*0.45f ;
            plants.triangle(p+vec3(-0.03f,0,0),p+vec3(0.03f,0,0),p+vec3(0.06f,h,0.03f),vec4(0.15f,0.30f,0.065f,0)) ;
        }
    }
}
