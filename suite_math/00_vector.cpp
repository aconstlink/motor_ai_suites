#include "test_support.hpp"
#include <motor/math/vector/vector2.hpp>
#include <motor/math/vector/vector3.hpp>
#include <motor/math/vector/vector4.hpp>

namespace this_file
{
    using namespace math_test ;

    template< size_t N, typename V >
    void_t arithmetic_for( context & t )
    {
        V a, b, sum, difference, product, quotient ;
        double_t dot = 0 ;
        for( size_t i = 0 ; i < N ; ++i )
        {
            float_t const x = float_t(int_t(i)*3-4), y = float_t(i+1) ;
            a[i] = x ; b[i] = y ;
            sum[i] = x+y ; difference[i] = x-y ; product[i] = x*y ; quotient[i] = x/y ;
            dot += double_t(x)*double_t(y) ;
        }
        t.elements<N>(a+b,sum,"addition") ;
        t.elements<N>(a-b,difference,"subtraction") ;
        t.elements<N>(a*b,product,"component multiplication") ;
        t.elements<N>(a/b,quotient,"component division") ;
        t.near(a.dot(b),dot,"dot product") ;
        t.elements<N>((a*2.0f)/2.0f,a,"scalar multiply/divide") ;
        t.elements<N>(-(-a),a,"double negation") ;
        {
            V value = a ; value += b ; t.elements<N>(value,sum,"+=") ;
            value = a ; value -= b ; t.elements<N>(value,difference,"-=") ;
            value = a ; value *= b ; t.elements<N>(value,product,"*=") ;
            value = a ; value /= b ; t.elements<N>(value,quotient,"/=") ;
            V copy = a ; copy[0] = 99 ; t.near(a[0],-4,"copy does not alias source") ;
        }
    }

    void_t arithmetic( context & t )
    {
        arithmetic_for<2,motor::math::vec2f_t>(t) ;
        arithmetic_for<3,motor::math::vec3f_t>(t) ;
        arithmetic_for<4,motor::math::vec4f_t>(t) ;
        arithmetic_for<3,motor::math::vector3<double_t>>(t) ;
    }

    void_t normalization( context & t )
    {
        {
            motor::math::vec2f_t const v(3,4) ;
            t.near(v.length2(),25,"vec2 squared length") ;
            t.near(v.length(),5,"vec2 length") ;
            t.elements<2>(v.normalized(),motor::math::vec2f_t(0.6f,0.8f),"vec2 normalization") ;
            float_t length = 0 ; auto copy = v ; copy.normalize(length) ;
            t.near(length,5,"normalize returns original length") ;
            t.near(copy.length(),1,"normalized vec2 is unit length") ;
            t.near(v.x(),3,"normalized() keeps source") ;
        }
        {
            motor::math::vec3f_t const v(2,-3,6) ;
            t.near(v.length(),7,"vec3 length") ;
            t.elements<3>(v.normalized(),v/7.0f,"vec3 normalization") ;
            auto copy = v ; copy.normalize() ; t.near(copy.length(),1,"in-place vec3 normalization") ;
            // vec3/vec4 explicitly guard zero length; no such promise is assumed for quaternions.
            t.elements<3>(motor::math::vec3f_t(0).normalized(),motor::math::vec3f_t(0),"zero vec3 stays finite") ;
        }
        {
            motor::math::vec4f_t const v(1,2,2,4) ;
            t.near(v.length(),5,"vec4 includes w in length") ;
            t.elements<4>(v.normalized(),v/5.0f,"vec4 normalization") ;
            t.elements<4>(motor::math::vec4f_t(0).normalized(),motor::math::vec4f_t(0),"zero vec4 stays finite") ;
        }
    }

    void_t cross( context & t )
    {
        using v3_t = motor::math::vec3f_t ;
        v3_t const x(1,0,0), y(0,1,0), z(0,0,1) ;
        t.elements<3>(x.crossed(y),z,"X cross Y is Z") ;
        t.elements<3>(y.crossed(x),-z,"cross product reverses sign") ;
        v3_t const a(2,-3,4), b(-1,5,2), expected(-26,-8,7) ;
        auto c = a ; c.cross(b) ;
        t.elements<3>(c,expected,"in-place cross fixed reference") ;
        t.near(c.dot(a),0,"cross perpendicular to first input") ;
        t.near(c.dot(b),0,"cross perpendicular to second input") ;
        c = a ; c.cross(c) ; t.elements<3>(c,v3_t(0),"self cross aliasing") ;
        motor::math::vec2f_t const p(2,3), q(4,-1) ;
        t.near(p.cross_as_scalar(q),-14,"2D oriented area") ;
        t.elements<3>(p.crossed(q),v3_t(0,0,-14),"2D cross lifted to Z") ;
    }
}

int main( int argc, char ** argv )
{
    math_test::test_case const cases[] = {
        {"arithmetic",this_file::arithmetic}, {"normalization",this_file::normalization},
        {"cross",this_file::cross} } ;
    return math_test::run(argc,argv,cases) ;
}
