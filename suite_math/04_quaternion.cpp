#include "test_support.hpp"
#include <motor/math/vector/vector3.hpp>
#include <motor/math/vector/vector4.hpp>
#include <motor/math/matrix/matrix3.hpp>
#include <motor/math/quaternion/quaternion4.hpp>
#include <motor/math/interpolation/slerp.hpp>

namespace this_file
{
    using namespace math_test ;
    using v3_t = motor::math::vec3f_t ;
    using q_t = motor::math::quat4f_t ;
    using interpolation_t = motor::math::interpolation<q_t> ;

    void_t axes( context & t )
    {
        v3_t const axes[] = {v3_t(1,0,0),v3_t(0,1,0),v3_t(0,0,1)} ;
        v3_t const positive[3][3] = {
            {v3_t(1,0,0),v3_t(0,0,1),v3_t(0,-1,0)},
            {v3_t(0,0,-1),v3_t(0,1,0),v3_t(1,0,0)},
            {v3_t(0,1,0),v3_t(-1,0,0),v3_t(0,0,1)} } ;
        for( size_t axis = 0 ; axis < 3 ; ++axis ) for( float_t const sign : {-1.0f,1.0f} )
        {
            auto const q = q_t::rotatate_norm_axis(axes[axis],sign*pi*0.5f) ;
            t.near(q.length(),1,"axis-angle quaternion unit length") ;
            for( size_t v = 0 ; v < 3 ; ++v )
            {
                auto const expected = v == axis ? axes[v] : positive[axis][v]*sign ;
                t.elements<3>(q*axes[v],expected,"signed 90-degree axis rotation") ;
                t.elements<3>(q.transform(axes[v]),expected,"transform matches quarter turn") ;
            }
        }
        {
            q_t const q(pi*0.5f,v3_t(0,0,5)) ;
            t.elements<3>(q*v3_t(1,0,0),v3_t(0,1,0),"angle-axis constructor normalizes axis") ;
            q_t const from_xyzw(motor::math::vec4f_t(0,0,std::sqrt(0.5f),std::sqrt(0.5f))) ;
            t.elements<3>(from_xyzw*v3_t(1,0,0),v3_t(0,1,0),"vec4 constructor takes xyzw") ;
            q_t const identity(1,0,0,0) ;
            t.elements<3>(identity*v3_t(2,-3,4),v3_t(2,-3,4),"explicit identity is wxyz 1,0,0,0") ;
        }
    }

    void_t composition( context & t )
    {
        auto const x = q_t::rotatate_norm_axis(v3_t(1,0,0),pi*0.5f) ;
        auto const y = q_t::rotatate_norm_axis(v3_t(0,1,0),pi*0.5f) ;
        t.elements<3>((y*x)*v3_t(0,1,0),v3_t(1,0,0),"qY*qX applies X first") ;
        t.elements<3>((x*y)*v3_t(0,1,0),v3_t(0,0,1),"quaternion product is not commutative") ;
        auto compound = y ; compound *= x ;
        t.elements<3>(compound*v3_t(0,1,0),v3_t(1,0,0),"compound multiplication") ;
        compound = x ; compound *= compound ;
        t.elements<3>(compound*v3_t(0,1,0),v3_t(0,-1,0),"self multiplication aliasing") ;
        auto const q = q_t(0.8f,v3_t(1,2,3)) ;
        v3_t const v(2,-1,4) ;
        t.elements<3>(q.conjugated()*(q*v),v,"unit conjugate reverses rotation") ;
        t.near((q*v).length(),v.length(),"rotation preserves vector length") ;
        q_t scaled(q.real()*3,q.i()*3,q.j()*3,q.k()*3) ;
        t.elements<3>(scaled.normalized()*v,q*v,"normalize non-unit quaternion") ;
        t.near(scaled.length(),3,"normalized leaves original quaternion unchanged") ;
        scaled.normalize() ; t.near(scaled.length(),1,"in-place normalization") ;
    }

    void_t matrix( context & t )
    {
        v3_t const axis = v3_t(1,2,-3).normalized(), v(3,-2,1) ;
        for( float_t const angle : {-2.3f,-0.7f,0.0f,0.9f,2.4f} )
        {
            // Rodrigues gives an independent scalar/vector reference for the rotation.
            auto const expected = v*std::cos(angle)+axis.crossed(v)*std::sin(angle)+
                axis*(axis.dot(v)*(1.0f-std::cos(angle))) ;
            auto const q = q_t::rotatate_norm_axis(axis,angle) ;
            auto const m = q.to_matrix() ;
            t.elements<3>(q*v,expected,"quaternion vs Rodrigues") ;
            t.elements<3>(m*v,expected,"quaternion matrix vs Rodrigues") ;
            t.elements<9>(m.transposed()*m,motor::math::mat3f_t::make_identity(),"rotation matrix orthogonal") ;
            q_t const negative(-q.real(),-q.i(),-q.j(),-q.k()) ;
            t.elements<9>(negative.to_matrix(),m,"q and -q represent same rotation") ;
        }
    }

    void_t slerp( context & t )
    {
        q_t const identity(1,0,0,0) ; v3_t const x(1,0,0), z(0,0,1) ;
        auto const quarter = q_t::rotatate_norm_axis(z,pi*0.5f) ;
        for( float_t const weight : {0.0f,0.25f,0.5f,0.75f,1.0f} )
        {
            auto const q = interpolation_t::linear(identity,quarter,weight) ;
            t.near(q.length(),1,"slerp unit length") ;
            t.elements<3>(q*x,v3_t(std::cos(weight*pi*0.5f),std::sin(weight*pi*0.5f),0),"slerp known arc") ;
        }
        {
            q_t const opposite(-quarter.real(),-quarter.i(),-quarter.j(),-quarter.k()) ;
            auto const q = interpolation_t::linear(quarter,opposite,0.5f) ;
            t.elements<3>(q*x,v3_t(0,1,0),"antipodal quaternion representations do not cancel") ;
            auto const same = interpolation_t::linear(quarter,quarter,0.4f) ;
            t.elements<3>(same*x,v3_t(0,1,0),"equal endpoints finite") ;
        }
        {
            auto const near = q_t::rotatate_norm_axis(z,0.001f) ;
            auto const q = interpolation_t::linear(identity,near,0.5f) ;
            t.elements<3>(q*x,v3_t(std::cos(0.0005f),std::sin(0.0005f),0),"near-equal endpoints linear fallback") ;
            auto const a = q_t::rotatate_norm_axis(z,170.0f*pi/180.0f) ;
            auto const b = q_t::rotatate_norm_axis(z,-170.0f*pi/180.0f) ;
            t.elements<3>(interpolation_t::linear(a,b,0.5f)*x,v3_t(-1,0,0),"shortest arc crosses 180 not zero") ;
        }
    }
}

int main( int argc, char ** argv )
{
    math_test::test_case const cases[] = {
        {"axes",this_file::axes}, {"composition",this_file::composition},
        {"matrix",this_file::matrix}, {"slerp",this_file::slerp} } ;
    return math_test::run(argc,argv,cases) ;
}
