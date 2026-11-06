#include "frustum_obj_models.hpp"
#include <motor/format/global.h>
#include <motor/format/module_registry.hpp>
#include <motor/format/future_items.hpp>
#include <motor/geometry/mesh/flat_tri_mesh.h>
#include <motor/io/database.h>
#include <motor/log/global.h>
#include <filesystem>
#include <limits>

namespace intersection_sample
{
    namespace
    {
        struct vertex { vec3_t position, normal ; } ;

        bool_t make_geometry( motor::format::mesh_item const & mesh, size_t const model_index,
            obj_models::model & out )
        {
            motor::vector<vertex> vertices ;
            motor::vector<uint_t> indices ;
            uint_t const invalid = std::numeric_limits<uint_t>::max() ;
            for( auto const & geo : mesh.geos )
            {
                auto const & p = geo.poly ;
                if( p.polygons.size() == 0 ) continue ;
                if( p.position_format != motor::geometry::vector_component_format::xyz || p.positions.size() % 3 != 0 ) return false ;
                size_t corner_count = 0 ;
                for( size_t k = 0 ; k < p.polygons.size() ; ++k )
                {
                    if( p.polygons[k] < 3 ) return false ;
                    corner_count += p.polygons[k] ;
                }
                if( corner_count != p.indices.size() ) return false ;
                for( size_t k = 0 ; k < p.indices.size() ; ++k )
                    if( p.indices[k] >= p.positions.size()/3 ) return false ;

                motor::geometry::flat_tri_mesh_t flat ;
                if( p.flatten( flat ) != motor::geometry::ok || !flat.are_array_numbers_valid() ||
                    flat.indices.size() % 3 != 0 ) return false ;
                size_t const count = flat.get_num_vertices(), offset = vertices.size() ;
                if( count > invalid - offset ) return false ;
                bool_t const has_normals = flat.get_num_normals() == count ;
                for( size_t k = 0 ; k < count ; ++k )
                {
                    auto position = flat.get_vertex_position_3d( k ) ;
                    auto normal = has_normals ? flat.get_vertex_normal_3d( k ) : vec3_t( 0 ) ;
                    // Lucy: the previous orientation plus 180 degrees about X.
                    if( model_index == 2 )
                    {
                        position = vec3_t( -position.x(), position.z(), position.y() ) ;
                        normal = vec3_t( -normal.x(), normal.z(), normal.y() ) ;
                    }
                    for( size_t axis = 0 ; axis < 3 ; ++axis )
                        if( !std::isfinite( position[axis] ) || !std::isfinite( normal[axis] ) ) return false ;
                    vertices.push_back( { position, normal } ) ;
                }
                for( size_t k = 0 ; k < flat.indices.size() ; k += 3 )
                {
                    if( flat.indices[k] >= count || flat.indices[k+1] >= count || flat.indices[k+2] >= count ) return false ;
                    uint_t const a = uint_t(offset) + flat.indices[k], b = uint_t(offset) + flat.indices[k+1],
                        c = uint_t(offset) + flat.indices[k+2] ;
                    auto const normal = ( vertices[b].position - vertices[a].position ).crossed(
                        vertices[c].position - vertices[a].position ) ;
                    if( normal.length2() <= 1e-30f ) continue ;
                    indices.push_back( a ) ; indices.push_back( b ) ; indices.push_back( c ) ;
                    if( !has_normals )
                    {
                        vertices[a].normal += normal ; vertices[b].normal += normal ; vertices[c].normal += normal ;
                    }
                }
            }
            if( vertices.empty() || indices.empty() ) return false ;
            vec3_t lower = vertices[0].position, upper = lower ;
            for( auto const & v : vertices ) for( size_t axis = 0 ; axis < 3 ; ++axis )
            {
                lower[axis] = std::min( lower[axis], v.position[axis] ) ;
                upper[axis] = std::max( upper[axis], v.position[axis] ) ;
            }
            auto const span = upper - lower ;
            float_t const longest = std::max( span.x(), std::max( span.y(), span.z() ) ) ;
            if( longest < 1e-8f ) return false ;
            float_t const scale = 3.6f / longest ;
            vec3_t const base( (lower.x()+upper.x())*.5f, lower.y(), (lower.z()+upper.z())*.5f ) ;
            for( auto & v : vertices )
            {
                v.position = ( v.position - base ) * scale ;
                v.normal = v.normal.length2() > 1e-30f ? v.normal.normalized() : vec3_t( 0, 1, 0 ) ;
            }
            out.bounds = hit_t::aabb_t( (upper-base)*scale, (lower-base)*scale ) ;
            auto const name = "frustum.obj.geometry." + motor::to_string( model_index ) ;
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element( motor::graphics::vertex_attribute::position, motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .add_layout_element( motor::graphics::vertex_attribute::normal, motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .resize( vertices.size() ).update<vertex>( [&]( vertex * ptr, size_t )
                { std::copy( vertices.begin(), vertices.end(), ptr ) ; } ) ;
            auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint )
                .resize( indices.size() ).update<uint_t>( [&]( uint_t * ptr, size_t )
                { std::copy( indices.begin(), indices.end(), ptr ) ; } ) ;
            out.geometry = motor::graphics::geometry_object_t( name, motor::graphics::primitive_type::triangles,
                std::move( vb ), std::move( ib ) ) ;
            out.vertices = vertices.size() ; out.triangles = indices.size()/3 ;
            motor::log::global_t::status( motor::string_t( "[frustum OBJ] " ) + obj_names[model_index] + ": " +
                motor::to_string( out.vertices ) + " vertices, " + motor::to_string( out.triangles ) + " triangles" ) ;
            return true ;
        }
    }

    bool_t obj_models::load( motor::string_cref_t directory )
    {
        if( attempted ) return loaded ;
        attempted = true ;
        char const * files[] = { "buddha.obj", "dragon.obj", "lucy.obj", "thai_statue.obj" } ;
        for( auto const file : files )
        {
            if( !std::filesystem::exists( std::filesystem::path( directory.c_str() ) / file ) )
            {
                motor::log::global_t::error( "[frustum OBJ] missing asset: " + directory + "/" + file ) ;
                return false ;
            }
        }
        auto db = motor::shared( motor::io::database_t( motor::io::path_t( directory.c_str() ), "./", "frustum_models" ) ) ;
        auto registry = motor::format::global::register_default_modules( motor::shared( motor::format::module_registry_t() ) ) ;
        bool_t ok = true ;
        for( size_t i = 0 ; i < obj_model_count && ok ; ++i )
        {
            motor::format::item_mtr_t item = nullptr ;
            try
            {
                item = registry->import_from( motor::io::location_t( files[i] ), "wavefront", db ).get() ;
                auto const * mesh = dynamic_cast<motor::format::mesh_item const *>( item ) ;
                ok = mesh != nullptr && make_geometry( *mesh, i, models[i] ) ;
            }
            catch( std::exception const & e )
            {
                motor::log::global_t::error( motor::string_t( "[frustum OBJ] " ) + e.what() ) ; ok = false ;
            }
            if( item != nullptr ) { item->release() ; motor::release( motor::move( item ) ) ; }
            motor::log::global_t::test( motor::string_t( "[frustum OBJ] import " ) + files[i], ok ) ;
        }
        motor::release( motor::move( registry ) ) ; motor::release( motor::move( db ) ) ;
        if( ok ) for( size_t i = 0 ; i < obj_instance_count ; ++i )
        {
            auto & obj = instances[i] ;
            size_t const row = i / 4, column = i % 4 ;
            obj.model_index = ( column + row ) % obj_model_count ;
            obj.position = vec3_t( -7.8f + float_t(column)*5.0f + (row == 1 ? .6f : 0),
                0, -5.5f + float_t(row)*5.5f ) ;
            auto const & local = models[obj.model_index].bounds ;
            auto const center = local.get_center() + obj.position ;
            obj.bounds = hit_t::aabb_t( center + local.get_extend(), center - local.get_extend() ) ;
        }
        loaded = ok ; return loaded ;
    }

    void_t obj_models::cull( frustum_t const & f, motor::vector<size_t> & visible,
        std::array<result_t, obj_instance_count> & results ) const
    {
        visible.clear() ;
        for( size_t i = 0 ; i < obj_instance_count ; ++i )
        {
            results[i] = hit_t::hit_test_frustum_aabb( f, instances[i].bounds ) ;
            if( results[i] != result_t::outside ) visible.push_back( i ) ;
        }
    }

    bool_t obj_models::checks( void_t ) const
    {
        bool_t ok = loaded ; size_t accepted = 0, rejected = 0 ;
        for( auto const & model : models ) ok &= model.triangles > 1000 && model.vertices > 0 ;
        auto const lucy_extent = models[2].bounds.get_extend() ;
        bool_t const upright = lucy_extent.y() > lucy_extent.x() && lucy_extent.y() > lucy_extent.z() ;
        motor::log::global_t::test( "[frustum OBJ] Lucy is upright on Y after orientation correction", upright ) ;
        ok &= upright ;
        std::array<size_t, obj_model_count> uses {} ;
        for( auto const & obj : instances )
        {
            if( obj.model_index >= obj_model_count ) { ok = false ; continue ; }
            ++uses[obj.model_index] ;
            auto const & local = models[obj.model_index].bounds ;
            auto const points = local.points(), world = obj.bounds.points() ;
            for( size_t p = 0 ; p < points.size() ; ++p ) ok &= (points[p]+obj.position-world[p]).length2() < 1e-8f ;
            ok &= std::abs( obj.bounds.get_center().y() - obj.bounds.get_extend().y() ) < 1e-5f ;
        }
        for( auto const use : uses ) ok &= use == 3 ;
        for( bool_t const ortho : { false, true } ) for( uint_t const width : { 480u, 1200u } )
        for( size_t step = 0 ; step < 60 ; ++step )
        {
            camera_t camera ;
            make_camera( camera, rectangle{0,0,width,800}, float_t(step), std::sin(float_t(step)), .82f, 18, ortho ) ;
            motor::vector<size_t> visible ; std::array<result_t, obj_instance_count> results ;
            cull( camera.get_frustum(), visible, results ) ;
            // Independent eight-corner reference for the conservative plane test.
            auto const f = camera.get_frustum() ;
            for( size_t i = 0 ; i < obj_instance_count ; ++i )
            {
                auto const points = instances[i].bounds.points() ;
                auto expected = result_t::inside ;
                for( size_t plane = 0 ; plane < 6 ; ++plane )
                {
                    auto const equation = f.get_plane( plane ) ;
                    bool_t all_outside = true ;
                    for( auto const & p : points )
                    {
                        float_t const distance = equation.xyz().dot( p ) + equation.w() ;
                        all_outside &= distance > 0 ;
                        if( distance >= 0 ) expected = result_t::intersect ;
                    }
                    if( all_outside ) { expected = result_t::outside ; break ; }
                }
                bool_t const submitted = std::find( visible.begin(), visible.end(), i ) != visible.end() ;
                ok &= results[i] == expected && submitted == ( expected != result_t::outside ) ;
                if( submitted ) ++accepted ; else ++rejected ;
            }
        }
        ok &= accepted > 0 && rejected > 0 ;
        motor::log::global_t::test( "[frustum OBJ] imports and shared AABB visibility across camera sweep", ok ) ;
        return ok ;
    }

    void_t obj_renderer::init( obj_models const & assets, motor::string_cref_t name )
    {
        if( shader != nullptr ) return ;
        shader = motor::shared( motor::graphics::msl_object_t( name, true ) ) ;
        shader->add( motor::graphics::msl_api_type::msl_4_0, R"(
            config frustum_scans
            {
                vertex_shader
                {
                    in vec3_t pos : position ; in vec3_t normal : normal ;
                    out vec4_t pos : position ; out vec3_t nrm : texcoord0 ;
                    mat4_t mvp ; vec3_t offset ;
                    void main() { out.pos = mvp * vec4_t( in.pos + offset, 1.0 ) ; out.nrm = in.normal ; }
                }
                pixel_shader
                {
                    in vec3_t nrm : texcoord0 ; out vec4_t color : color ; vec4_t base_color ;
                    void main()
                    {
                        vec3_t n = normalize( in.nrm ) ;
                        float_t key = max( dot( n, normalize( vec3_t( 0.4, 0.8, 0.6 ) ) ), 0.0 ) ;
                        float_t fill = max( dot( n, normalize( vec3_t( -0.6, 0.3, -0.4 ) ) ), 0.0 ) ;
                        out.color = vec4_t( base_color.xyz * ( 0.20 + 0.65 * key + 0.22 * fill ), 1.0 ) ;
                    }
                }
            }
        )" ) ;
        for( auto const & model : assets.models ) shader->link_geometry( model.geometry.name() ) ;
        for( auto & view : slots ) for( size_t i = 0 ; i < obj_instance_count ; ++i )
        {
            auto & s = view[i] ;
            s.geometry = assets.instances[i].model_index ;
            auto vs = motor::shared( motor::graphics::variable_set_t() ) ;
            vs->data_variable<vec3_t>( "offset" )->set( assets.instances[i].position ) ;
            s.mvp = vs->data_variable<mat4_t>( "mvp" ) ; s.mvp->set( mat4_t::make_identity() ) ;
            s.color = vs->data_variable<vec4_t>( "base_color" ) ; s.color->set( vec4_t( 1 ) ) ;
            s.id = shader->add_variable_set( motor::move( vs ) ) ;
        }
    }

    void_t obj_renderer::prepare( size_t view, camera_t const & camera, std::array<result_t, obj_instance_count> const & results )
    {
        vec4_t const colors[] = { {.75f,.57f,.25f,1}, {.22f,.68f,.56f,1}, {.78f,.81f,.90f,1}, {.65f,.38f,.32f,1} } ;
        auto const mvp = camera.mat_proj() * camera.mat_view() ;
        for( size_t i = 0 ; i < obj_instance_count ; ++i )
        {
            slots[view][i].mvp->set( mvp ) ;
            slots[view][i].color->set( results[i] == result_t::intersect ? vec4_t( 1,.48f,.16f,1 ) : colors[slots[view][i].geometry] ) ;
        }
    }
    void_t obj_renderer::configure( motor::graphics::gen4::frontend_ptr_t fe, obj_models & assets )
    {
        if( !configured )
        {
            for( auto & model : assets.models ) fe->configure<motor::graphics::geometry_object_t>( &model.geometry ) ;
            fe->configure<motor::graphics::msl_object_t>( shader ) ; configured = true ;
        }
        auto const status = fe->decode( shader ) ;
        ready = status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
    }
    void_t obj_renderer::render( motor::graphics::gen4::frontend_ptr_t fe, size_t view, motor::vector<size_t> const & visible )
    {
        if( !ready ) return ;
        for( auto const i : visible )
        {
            motor::graphics::gen4::backend_t::render_detail_t detail ;
            detail.geo = slots[view][i].geometry ; detail.varset = slots[view][i].id ; fe->render( shader, detail ) ;
        }
    }
    void_t obj_renderer::release_backend( motor::graphics::gen4::frontend_ptr_t fe, obj_models & assets )
    {
        if( !configured ) return ;
        fe->release<motor::graphics::msl_object_t>( shader ) ;
        for( auto & model : assets.models ) fe->release<motor::graphics::geometry_object_t>( &model.geometry ) ;
        configured = false ; ready = false ;
    }
    void_t obj_renderer::release( void_t ) { if( shader ) motor::release( motor::move( shader ) ) ; }
}
