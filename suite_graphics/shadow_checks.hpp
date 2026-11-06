#pragma once

#include "shadow_camera_fit.hpp"
#include <motor/format/global.h>
#include <motor/io/database.h>
#include <motor/msl/parser.h>
#include <motor/msl/database.hpp>
#include <motor/msl/dependency_resolver.hpp>
#include <motor/msl/generators/generator.h>
#include <motor/msl/generators/glsl4_generator.h>
#include <motor/msl/generators/hlsl5_generator.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/variable_update_visitor.hpp>
#include <motor/scene/visitor/light_collector_visitor.hpp>
#ifdef _WIN32
#include <d3dcompiler.h>
#endif

namespace shadow_sample
{
    inline bool_t check_assets( void_t )
    {
        motor::io::database_mtr_t db = motor::shared( motor::io::database_t(
            motor::io::path_t( MOTOR_SHADOW_ASSETS ), ".", "shadow29_cpu" ) );
        bool_t passed = true;
        {
            auto registry = motor::format::global::register_default_modules(
                motor::shared( motor::format::module_registry_t() ) );
            motor::io::database_mtr_t scene_db = motor::shared( motor::io::database_t(
                motor::io::path_t( MOTOR_SPONZA_ASSETS ), ".", "shadow29_sponza_cpu" ) );
            auto * item = registry->import_from( motor::io::location_t(
                "Sponza.Sponza.gltf" ), scene_db ).get();
            auto * scene = dynamic_cast< motor::format::scene_item_ptr_t >( item );
            bool_t scene_ok = scene && scene->root;
            if( scene_ok )
            {
                motor::scene::variable_update_visitor_t variables;
                motor::scene::node_t::traverser( scene->root ).apply( &variables );
                motor::scene::trafo_visitor_t transforms;
                motor::scene::node_t::traverser( scene->root ).apply( &transforms );
                auto const bounds = transforms.get_world_box();
                motor::gfx::generic_camera_t sun;
                fit_light( sun, bounds, sun_direction() );
                scene_ok &= encloses( sun, bounds );
                for( size_t frame = 0; frame <= 320; ++frame )
                {
                    auto const direction = sun_direction( float_t(frame) * 0.25f );
                    fit_light( sun, bounds, direction );
                    scene_ok &= encloses( sun, bounds ) && direction.y() > 0.0f;
                }
                scene_ok &= (sun_direction( 80.0f ) - sun_direction()).length() < 0.0001f;
                auto const dims = bounds.get_dims();
                // Detect accidentally replacing the importer's centimetre-to-scene scale.
                scene_ok &= dims.x() > 15.0f && dims.x() < 50.0f && dims.y() > 5.0f && dims.y() < 30.0f;
                motor::log::global_t::status( "[shadow29] Sponza bounds: " + motor::to_string(dims.x()) +
                    " x " + motor::to_string(dims.y()) + " x " + motor::to_string(dims.z()) );
                for( float_t const aspect : { 16.0f/9.0f, 1.0f, 9.0f/16.0f } )
                {
                    motor::gfx::generic_camera_t camera;
                    float_t min_height = bounds.get_max().y(), max_height = bounds.get_min().y();
                    for( size_t frame = 0; frame <= 160; ++frame )
                    {
                        sponza_camera( camera, bounds, aspect, float_t(frame) * 0.25f );
                        auto const eye = camera.get_position();
                        min_height = std::min( min_height, eye.y() );
                        max_height = std::max( max_height, eye.y() );
                        scene_ok &= bounds.is_inside( eye );
                        auto const vp = camera.get_proj_matrix() * camera.get_view_matrix();
                        for( size_t n = 0; n < 16; ++n ) scene_ok &= std::isfinite( vp[n] );
                    }
                    motor::gfx::generic_camera_t start;
                    sponza_camera( start, bounds, aspect, 0.0f );
                    scene_ok &= (start.get_position() - camera.get_position()).length() < 0.001f;
                    scene_ok &= max_height - min_height > 4.0f;
                }
            }
            motor::log::global_t::test( "Sponza scale, 80-second sun fit and 40-second camera path", scene_ok );
            passed &= scene_ok;
            if( item ) { item->release(); motor::release( motor::move( item ) ); }
            motor::release( motor::move( scene_db ) );
            motor::release( motor::move( registry ) );
        }
        motor::vector< motor::msl::symbol_t > all_symbols;
        for( auto const * name : { "color_pass", "depth_pass", "normal_vis_pass",
            "shadow_light_pass", "light_pass", "shadow_depth_pass" } )
        {
            motor::string_t source;
            auto access = db->load( motor::io::location_t( motor::string_t(
                "29_camera_fit_shadow_mapping.shaders." ) + name + ".msl" ) );
            access.wait_for_operation( [&]( char const * data, size_t const count, motor::io::result const result )
            {
                if( result == motor::io::result::ok ) source.assign( data, count );
            } );
            motor::msl::database_t database;
            motor::vector< motor::msl::symbol_t > symbols;
            database.insert( motor::msl::parser_t( "shadow29_cpu" ).process( source ), symbols );
            bool_t shader_ok = !symbols.empty();
            for( auto const & symbol : symbols )
            {
                bool_t const unique = std::find( all_symbols.begin(), all_symbols.end(), symbol ) == all_symbols.end();
                motor::log::global_t::test( "Unique sample shader config: " + symbol.expand(), unique );
                shader_ok &= unique;
                all_symbols.emplace_back( symbol );
                auto resolved = motor::msl::dependency_resolver_t().resolve( &database, symbol );
                shader_ok &= resolved.missing.empty();
                motor::msl::generator_t generator( std::move( resolved ) );
                auto const glsl = generator.generate< motor::msl::glsl::glsl4_generator_t >();
                auto const hlsl = generator.generate< motor::msl::hlsl::hlsl5_generator_t >();
                shader_ok &= glsl.shaders.size() == 2 && hlsl.shaders.size() == 2;
#ifdef _WIN32
                for( auto const & shader : hlsl.shaders )
                {
                    for( auto const & code : shader.codes )
                    {
                        ID3DBlob * binary = nullptr, * errors = nullptr;
                        auto const profile = shader.type == motor::msl::shader_type::vertex_shader ? "vs_5_0" : "ps_5_0";
                        auto const entry = shader.type == motor::msl::shader_type::vertex_shader ? "VS" : "PS";
                        auto const hr = D3DCompile( code.shader.data(), code.shader.size(), name,
                            nullptr, nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &binary, &errors );
                        if( FAILED( hr ) && errors ) motor::log::global_t::error(
                            motor::string_t( static_cast< char const * >( errors->GetBufferPointer() ), errors->GetBufferSize() ) );
                        shader_ok &= SUCCEEDED( hr );
                        if( binary ) binary->Release();
                        if( errors ) errors->Release();
                    }
                }
#endif
            }
            motor::log::global_t::test( motor::string_t( "Shadow shader generation/compile: " ) + name, shader_ok );
            passed &= shader_ok;
        }
        motor::release( motor::move( db ) );
        return passed;
    }
}
