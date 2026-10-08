#include "test_support.hpp"
#include <motor/math/utility/3d/transformation.hpp>
#include <motor/math/camera/3d/camera_util.hpp>
#include <motor/math/camera/3d/perspective_fov.hpp>
#include <motor/math/camera/3d/orthographic_projection.hpp>

namespace this_file
{
    using namespace math_test ;
    using v3_t = motor::math::vec3f_t ;
    using v4_t = motor::math::vec4f_t ;
    using m4_t = motor::math::mat4f_t ;
    using trafo_t = motor::math::m3d::trafof_t ;

    v4_t point( trafo_t const & trafo, v3_t const & p )
    {
        return trafo.get_transformation()*v4_t(p,1) ;
    }

    void_t trs( context & t )
    {
        {
            trafo_t const identity ;
            t.elements<16>(identity.get_transformation(),m4_t::make_identity(),"default transform identity") ;
        }
        {
            trafo_t const value(v3_t(2,3,4),v3_t(0,0,pi*0.5f),v3_t(10,20,30)) ;
            t.elements<4>(point(value,v3_t(1,2,3)),v4_t(4,22,42,1),"TRS: scale then rotate then translate") ;
            t.elements<3>(value.get_translation(),v3_t(10,20,30),"TRS translation extraction") ;
            t.elements<3>(value.get_scale(),v3_t(2,3,4),"TRS positive nonuniform scale extraction") ;
            auto copy = value ; copy.set_translation(v3_t(-2,5,7)) ;
            t.elements<4>(point(copy,v3_t(1,2,3)),v4_t(-8,7,19,1),"replace translation keeps rotation and scale") ;
            t.elements<3>(value.get_translation(),v3_t(10,20,30),"transform copy independent") ;
        }
        {
            trafo_t const axis(v3_t(2,3,4),v4_t(0,0,1,pi*0.5f),v3_t(10,20,30)) ;
            t.elements<4>(point(axis,v3_t(1,2,3)),v4_t(4,22,42,1),"axis-angle TRS constructor") ;
        }
    }

    void_t spaces( context & t )
    {
        {
            auto world = trafo_t::rotation_by_axis(v3_t(0,0,1),pi*0.5f) ;
            auto local = world ;
            world.translate_fl(v3_t(2,0,0)) ; local.translate_fr(v3_t(2,0,0)) ;
            t.elements<4>(point(world,v3_t(0)),v4_t(2,0,0,1),"left translation in parent space") ;
            t.elements<4>(point(local,v3_t(0)),v4_t(0,2,0,1),"right translation in local space") ;
        }
        {
            auto orbit = trafo_t::translation(v3_t(2,0,0)) ; auto spin = orbit ;
            orbit.rotate_by_axis_fl(v3_t(0,0,1),pi*0.5f) ;
            spin.rotate_by_axis_fr(v3_t(0,0,1),pi*0.5f) ;
            t.elements<4>(point(orbit,v3_t(0)),v4_t(0,2,0,1),"left rotation moves origin") ;
            t.elements<4>(point(spin,v3_t(0)),v4_t(2,0,0,1),"right rotation preserves origin") ;
        }
        {
            auto world = trafo_t::translation(v3_t(2,3,4)) ; auto local = world ;
            world.scale_fl(v3_t(2,3,4)) ; local.scale_fr(v3_t(2,3,4)) ;
            t.elements<4>(point(world,v3_t(0)),v4_t(4,9,16,1),"left scale affects translation") ;
            t.elements<4>(point(local,v3_t(0)),v4_t(2,3,4,1),"right scale preserves translation") ;
        }
    }

    void_t hierarchy( context & t )
    {
        trafo_t const parent(v3_t(2),v3_t(0,0,pi*0.5f),v3_t(10,0,0)) ;
        auto const child = trafo_t::translation(v3_t(3,0,0)) ;
        auto const grandchild = trafo_t::translation(v3_t(0,1,0)) ;
        auto const world = parent*child*grandchild ;
        t.elements<4>(point(world,v3_t(0)),v4_t(8,6,0,1),"three-level hierarchy fixed reference") ;
        auto left = child ; left.transform_fl(parent) ;
        auto right = parent ; right.transform_fr(child) ;
        t.elements<16>(left.get_transformation(),right.get_transformation(),"transform_fl/fr composition") ;
        auto compound = parent ; compound *= child ; compound *= grandchild ;
        t.elements<16>(compound.get_transformation(),world.get_transformation(),"compound hierarchy") ;
        auto const reversed = child*parent ;
        t.elements<4>(point(reversed,v3_t(0)),v4_t(13,0,0,1),"reversed hierarchy has different origin") ;
    }

    void_t camera( context & t )
    {
        using camera_t = motor::math::m3d::camera_util<float_t> ;
        {
            m4_t frame, view ;
            camera_t::create_lookat(v3_t(0,0,5),v3_t(0,1,0),v3_t(0),frame) ;
            camera_t::create_view_matrix(frame,view) ;
            t.elements<4>(view*v4_t(0,0,0,1),v4_t(0,0,-5,1),"target lies on negative view Z") ;
            t.elements<4>(view*v4_t(2,1,0,1),v4_t(2,1,-5,1),"view preserves right and up") ;
            t.elements<4>(view*v4_t(0,0,5,1),v4_t(0,0,0,1),"camera position maps to origin") ;
            t.elements<16>(view*frame,m4_t::make_identity(),"view inverts camera frame") ;
        }
        {
            m4_t frame, view ;
            v3_t const position(4,3,7), target(-2,1,0) ;
            camera_t::create_lookat(position,target,frame) ;
            camera_t::create_view_matrix(frame,view) ;
            t.elements<16>(view*frame,m4_t::make_identity(),"oblique view inverse") ;
            t.elements<4>(view*v4_t(target,1),v4_t(0,0,-(position-target).length(),1),"oblique target centered") ;
        }
    }

    void_t projection( context & t )
    {
        using perspective_t = motor::math::m3d::perspective<float_t> ;
        {
            // This API fills selected coefficients, so pass a zero-initialized matrix.
            m4_t p ; perspective_t::create_by_fovv_aspect(pi*0.5f,2.0f,1.0f,11.0f,p) ;
            auto const near = p*v4_t(0,0,-1,1), far = p*v4_t(0,0,-11,1) ;
            t.near(near.w(),1,"perspective clip W is -view Z") ;
            t.near(near.z()/near.w(),-1,"perspective near NDC -1") ;
            t.near(far.z()/far.w(),1,"perspective far NDC +1") ;
            auto const edge = p*v4_t(2,1,-1,1) ;
            t.near(edge.x()/edge.w(),1,"perspective horizontal edge") ;
            t.near(edge.y()/edge.w(),1,"perspective vertical edge") ;
            auto const half = p*v4_t(2,1,-2,1) ;
            t.near(half.x()/half.w(),0.5,"perspective foreshortening") ;
        }
        {
            auto const p = motor::math::m3d::orthographic<float_t>::create(8,4,1,11) ;
            t.elements<4>(p*v4_t(4,2,-1,1),v4_t(1,1,-1,1),"orthographic near corner") ;
            t.elements<4>(p*v4_t(-4,-2,-11,1),v4_t(-1,-1,1,1),"orthographic far corner") ;
        }
        {
            float_t const vertical = pi/3.0f, aspect = 16.0f/9.0f ;
            float_t const horizontal = perspective_t::fovh_for_fovv_aspect(vertical,aspect) ;
            t.near(perspective_t::fovv_for_fovh_aspect(horizontal,aspect),vertical,"FOV conversion roundtrip") ;
        }
    }
}

int main( int argc, char ** argv )
{
    math_test::test_case const cases[] = {
        {"trs",this_file::trs}, {"spaces",this_file::spaces}, {"hierarchy",this_file::hierarchy},
        {"camera",this_file::camera}, {"projection",this_file::projection} } ;
    return math_test::run(argc,argv,cases) ;
}
