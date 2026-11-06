#pragma once

#include "frustum_intersections.hpp"
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>

namespace intersection_sample
{
    inline constexpr size_t obj_model_count = 4 ;
    inline constexpr size_t obj_instance_count = 12 ;
    inline constexpr char const * obj_names[] = { "Happy Buddha", "Stanford Dragon", "Lucy", "Thai Statue" } ;

    struct obj_models
    {
        struct model
        {
            motor::graphics::geometry_object_t geometry ;
            hit_t::aabb_t bounds ;
            size_t vertices = 0, triangles = 0 ;
        } ;
        std::array<model, obj_model_count> models ;
        struct instance
        {
            size_t model_index = 0 ;
            vec3_t position ;
            hit_t::aabb_t bounds ;
        } ;
        std::array<instance, obj_instance_count> instances ;
        bool_t attempted = false, loaded = false ;
        bool_t load( motor::string_cref_t directory ) ;
        void_t cull( frustum_t const &, motor::vector<size_t> &, std::array<result_t, obj_instance_count> & ) const ;
        bool_t checks( void_t ) const ;
    } ;

    struct obj_renderer
    {
        struct slot
        {
            size_t id = 0 ;
            size_t geometry = 0 ;
            motor::graphics::data_variable<mat4_t> * mvp = nullptr ;
            motor::graphics::data_variable<vec4_t> * color = nullptr ;
        } ;
        motor::graphics::msl_object_mtr_t shader = nullptr ;
        std::array<std::array<slot, obj_instance_count>, 2> slots ;
        bool_t configured = false, ready = false ;
        void_t init( obj_models const &, motor::string_cref_t ) ;
        void_t prepare( size_t, camera_t const &, std::array<result_t, obj_instance_count> const & ) ;
        void_t configure( motor::graphics::gen4::frontend_ptr_t, obj_models & ) ;
        void_t render( motor::graphics::gen4::frontend_ptr_t, size_t, motor::vector<size_t> const & ) ;
        void_t release_backend( motor::graphics::gen4::frontend_ptr_t, obj_models & ) ;
        void_t release( void_t ) ;
    } ;
}
