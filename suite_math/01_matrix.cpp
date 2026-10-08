#include "test_support.hpp"
#include <motor/math/vector/vector2.hpp>
#include <motor/math/vector/vector3.hpp>
#include <motor/math/vector/vector4.hpp>
#include <motor/math/matrix/matrix2.hpp>
#include <motor/math/matrix/matrix3.hpp>
#include <motor/math/matrix/matrix4.hpp>

namespace this_file
{
    using namespace math_test ;

    template< size_t N, typename M >
    void_t products_for( context & t )
    {
        for( int_t fixture = 0 ; fixture < 12 ; ++fixture )
        {
            t.section = "mat"+motor::to_string(N)+", fixture "+motor::to_string(fixture)+": " ;
            M a, b, expected, square, identity ; identity.identity() ;
            // Independent scalar reference over signed, nonsymmetric integer fixtures.
            for( size_t i = 0 ; i < N*N ; ++i )
            {
                a[i] = float_t((int_t(i)*7+fixture*3)%13-6) ;
                b[i] = float_t((int_t(i)*3+fixture*5)%11-5) ;
            }
            for( size_t r = 0 ; r < N ; ++r ) for( size_t c = 0 ; c < N ; ++c )
            {
                double_t ab = 0, aa = 0 ;
                for( size_t k = 0 ; k < N ; ++k )
                {
                    ab += double_t(a[r*N+k])*double_t(b[k*N+c]) ;
                    aa += double_t(a[r*N+k])*double_t(a[k*N+c]) ;
                }
                expected[r*N+c] = float_t(ab) ; square[r*N+c] = float_t(aa) ;
            }
            t.elements<N*N>(a*b,expected,"matrix product vs scalar reference") ;
            M inplace = a ; inplace *= b ; t.elements<N*N>(inplace,expected,"matrix *=") ;
            inplace = a ; inplace *= inplace ; t.elements<N*N>(inplace,square,"matrix *= self aliasing") ;
            t.elements<N*N>(identity*a,a,"left identity") ;
            t.elements<N*N>(a*identity,a,"right identity") ;
        }
    }

    void_t products( context & t )
    {
        products_for<2,motor::math::matrix2<float_t>>(t) ;
        products_for<3,motor::math::mat3f_t>(t) ;
        products_for<4,motor::math::mat4f_t>(t) ;
        products_for<4,motor::math::matrix4<double_t>>(t) ;
    }

    template< size_t N, typename M >
    void_t transpose_for( context & t )
    {
        t.section = "mat"+motor::to_string(N)+": " ;
        M a ; for( size_t i = 0 ; i < N*N ; ++i ) a[i] = float_t(i+1) ;
        M b = a ; b.transpose() ;
        for( size_t r = 0 ; r < N ; ++r ) for( size_t c = 0 ; c < N ; ++c )
        {
            t.near(a(r,c),double_t(r*N+c+1),"row/column access") ;
            t.near(b[r*N+c],double_t(c*N+r+1),"transpose swaps row/column") ;
            t.near(a.get_column(c)[r],double_t(r*N+c+1),"column access agrees with reference") ;
        }
        b.transpose() ; t.elements<N*N>(b,a,"double transpose") ;
    }

    void_t transpose( context & t )
    {
        transpose_for<2,motor::math::matrix2<float_t>>(t) ;
        transpose_for<3,motor::math::mat3f_t>(t) ;
        transpose_for<4,motor::math::mat4f_t>(t) ;
    }

    void_t homogeneous( context & t )
    {
        using v3_t = motor::math::vec3f_t ; using v4_t = motor::math::vec4f_t ;
        auto m = motor::math::mat4f_t::make_identity() ;
        m.set_row(0,v4_t(2,0,0,10)) ; m.set_row(1,v4_t(0,3,0,-2)) ;
        m.set_row(2,v4_t(0,0,4,5)) ;
        t.elements<4>(m*v4_t(1,2,3,1),v4_t(12,4,17,1),"homogeneous point includes translation") ;
        t.elements<4>(m*v4_t(1,2,3,0),v4_t(2,6,12,0),"homogeneous direction ignores translation") ;
        t.elements<3>(m*v3_t(1,2,3),v3_t(2,6,12),"mat4 * vec3 uses upper 3x3 only") ;
        t.elements<4>(m.get_column(3),v4_t(10,-2,5,1),"translation is fourth column") ;
    }

    void_t rotation2( context & t )
    {
        using m2_t = motor::math::matrix2<float_t> ; using v2_t = motor::math::vec2f_t ;
        auto r = m2_t::rotation(pi*0.5f) ;
        t.elements<2>(r*v2_t(1,0),v2_t(0,1),"2D positive quarter turn") ;
        auto id = m2_t() ; id.identity() ;
        t.near(id.angle(),0,"2D identity angle") ;
        t.near(r.angle(),pi*0.5f,"2D quarter-turn angle") ;
    }
}

int main( int argc, char ** argv )
{
    math_test::test_case const cases[] = {
        {"products",this_file::products}, {"transpose",this_file::transpose},
        {"homogeneous",this_file::homogeneous}, {"rotation2",this_file::rotation2} } ;
    return math_test::run(argc,argv,cases) ;
}
