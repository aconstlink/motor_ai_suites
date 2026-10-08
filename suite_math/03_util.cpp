#include "test_support.hpp"
#include <motor/math/utility/fn.hpp>
#include <motor/math/utility/angle.hpp>
#include <motor/math/utility/time.hpp>
#include <motor/math/utility/index_1d_to_2d.hpp>
#include <motor/math/utility/index_2d_to_1d.hpp>
#include <motor/math/utility/3d/ortho_basis.hpp>

namespace this_file
{
    using namespace math_test ;
    using fn_t = motor::math::fn<float_t> ;
    using v3_t = motor::math::vec3f_t ;
    using basis_t = motor::math::m3d::orthonormal_basis<float_t> ;

    void_t scalar( context & t )
    {
        for( float_t const x : {-2.0f,-0.25f,0.0f,0.5f,1.0f,3.0f} )
        {
            t.near(fn_t::abs(x),std::abs(x),"abs") ;
            t.near(fn_t::saturate(x),x < 0 ? 0 : (x > 1 ? 1 : x),"saturate") ;
            t.near(fn_t::sign(x),x < 0 ? -1 : (x > 0 ? 1 : 0),"sign including zero") ;
            t.near(fn_t::pnv_to_nnv(fn_t::nnv_to_pnv(x)),x,"signed/unsigned normalized mapping") ;
        }
        t.near(fn_t::clamp(-4,-2,3),-2,"clamp low") ;
        t.near(fn_t::clamp(4,-2,3),3,"clamp high") ;
        t.near(fn_t::step(2,2),1,"step inclusive threshold") ;
        t.near(fn_t::pulse(3,2,3),0,"pulse excludes upper bound") ;
        t.near(fn_t::mix(0.25f,2,10),4,"mix takes interpolation weight first") ;
        t.near(fn_t::box_step(4,2,6),0.5,"box step midpoint") ;
        for( float_t const x : {0.0f,0.25f,0.5f,1.0f} )
        {
            t.near(fn_t::smooth_step(x),3*x*x-2*x*x*x,"cubic smooth step") ;
            t.near(fn_t::smooth_step_e5(x),6*std::pow(x,5)-15*std::pow(x,4)+10*std::pow(x,3),"quintic smooth step") ;
        }
    }

    void_t rounding( context & t )
    {
        for( float_t const x : {-2.5f,-2.0f,-0.1f,0.0f,0.1f,1.0f,1.5f,2.0f} )
        {
            auto const label = motor::string_t("ceil(")+motor::to_string(x)+")" ;
            t.near(fn_t::ceil(x),std::ceil(x),label.c_str()) ;
            t.near(fn_t::floor(x),std::floor(x),"floor vs standard library") ;
            t.near(fn_t::fract(x),double_t(x)-std::floor(double_t(x)),"fract including negative inputs") ;
        }
    }

    void_t angles( context & t )
    {
        using angle_t = motor::math::angle<float_t> ;
        t.near(angle_t::degree_to_radian(180),pi,"180 degrees is pi") ;
        t.near(angle_t::radian_to_degree(pi*0.5f),90,"half pi is 90 degrees") ;
        angle_t value(90,angle_t::degrees()) ; value += pi*0.5f ;
        t.near(value.as_degrees(),180,"angle accumulation") ;
        for( float_t const x : {0.0f,pi*0.5f,pi*2.5f,-pi*0.5f,-pi*2.5f} )
        {
            double_t const turn = double_t(pi)*2 ;
            double_t expected = std::fmod(double_t(x),turn) ;
            if( expected < 0 ) expected += turn ;
            auto const label = motor::string_t("constrain_angle(")+motor::to_string(x)+")" ;
            t.near(angle_t::constrain_angle(x),expected,label.c_str()) ;
        }
    }

    void_t indexing( context & t )
    {
        motor::math::index_1d_to_2d<size_t> const grid(7,5) ;
        for( size_t i = 0 ; i < 35 ; ++i )
        {
            auto const xy = grid.to_index(i) ;
            t.check(xy.x() == i%7 && xy.y() == i/7,"1D to 2D nonsquare grid") ;
            motor::math::index_2d_to_1d<size_t> cursor(7) ;
            for( size_t y = 0 ; y < xy.y() ; ++y ) cursor.add_2d() ;
            for( size_t x = 0 ; x < xy.x() ; ++x ) cursor.add_1d() ;
            t.check(cursor.get_index() == i,"2D index cursor roundtrip") ;
        }
        t.check(grid.to_index(35).x() == 0 && grid.to_index(35).y() == 0,"index wraps at grid size") ;
    }

    void_t time( context & t )
    {
        using time_util_t = motor::math::time ;
        t.check(time_util_t::to_milli(2,3,4) == 123004,"minutes seconds milliseconds") ;
        for( size_t const value : {size_t(0),size_t(999),size_t(1000),size_t(60000),
            size_t(3600000),size_t(86400000),size_t(90061007)} )
        {
            t.section = "timestamp "+motor::to_string(value)+" ms: " ;
            time_util_t::time_info info = {} ; time_util_t::milli_to(value,info) ;
            t.check(info.mil == value%1000,"millisecond remainder") ;
            t.check(info.sec == (value/1000)%60,"second remainder") ;
            t.check(info.min == (value/60000)%60,"minute remainder") ;
            t.check(info.hour == (value/3600000)%24,"hour must wrap at 24") ;
            t.check(info.day == value/86400000,"day count") ;
            size_t const rebuilt = ((((info.day*24)+info.hour)*60+info.min)*60+info.sec)*1000+info.mil ;
            t.check(rebuilt == value,"time decomposition reconstructs original timestamp") ;
        }
    }

    void_t check_basis( context & t, v3_t const & dir, v3_t const & up )
    {
        t.section = "basis dir ("+motor::to_string(dir.x())+","+motor::to_string(dir.y())+","+
            motor::to_string(dir.z())+"): " ;
        v3_t x, y, z ; basis_t::create(dir,up,x,y,z) ;
        t.near(x.length(),1,"basis X unit") ; t.near(y.length(),1,"basis Y unit") ;
        t.near(z.length(),1,"basis Z unit") ;
        t.near(x.dot(y),0,"basis X perpendicular Y") ;
        t.near(x.dot(z),0,"basis X perpendicular Z") ;
        t.near(y.dot(z),0,"basis Y perpendicular Z") ;
        t.elements<3>(x.crossed(y),z,"basis right handed") ;
        t.elements<3>(z,dir,"basis retains normalized direction") ;
    }

    void_t basis( context & t )
    {
        for( auto const & dir : {v3_t(0,0,1),v3_t(0,0,-1),v3_t(1,2,3).normalized(),v3_t(-3,1,2).normalized()} )
            check_basis(t,dir,v3_t(0,1,0)) ;
    }

    void_t collinear_basis( context & t )
    {
        // The implementation explicitly promises an alternative up vector for collinearity.
        check_basis(t,v3_t(0,1,0),v3_t(0,1,0)) ;
        check_basis(t,v3_t(0,-1,0),v3_t(0,1,0)) ;
        check_basis(t,v3_t(0,0,1),v3_t(0,0,1)) ;
        check_basis(t,v3_t(0,0,-1),v3_t(0,0,1)) ;
    }
}

int main( int argc, char ** argv )
{
    math_test::test_case const cases[] = {
        {"scalar",this_file::scalar}, {"rounding",this_file::rounding}, {"angles",this_file::angles},
        {"indexing",this_file::indexing}, {"time",this_file::time},
        {"basis",this_file::basis}, {"collinear_basis",this_file::collinear_basis} } ;
    return math_test::run(argc,argv,cases) ;
}
