#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/scene/node/logic_group.h>
#include <motor/scene/node/logic_leaf.h>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/graphics/msl_set_component.hpp>
#include <motor/scene/visitor/variable_update_visitor.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/geometry/3d/cube.h>
#include <motor/geometry/mesh/tri_mesh.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/gfx/postprocess/hdr_postprocess_pipeline.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/tool/imgui/imgui_property.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <chrono>
#include <cmath>
#include <type_traits>

namespace sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using mat4_t = motor::math::mat4f_t ;
    using trafo_t = motor::math::m3d::trafof_t ;
    template< typename T > using output_t = motor::wire::output_slot<T> ;

    struct options
    {
        bool_t dual = true, d3d_only = false, smoke = false, still = false ;
    } ;

    // The component owns the slots; the app and connected consumers borrow them.
    class lighting_controls : public motor::scene::icomponent
    {
    public:
        output_t<vec3_t> * direction[3] = {}, * energy[3] = {} ;
        output_t<float_t> * ambient = nullptr ;
        output_t<trafo_t> * group_pose = nullptr ;

        lighting_controls( void_t ) noexcept
        {
            for( size_t l = 0 ; l < 3 ; ++l )
            {
                direction[l] = create_output_slot<vec3_t>( vec3_t(0,1,0) ) ;
                energy[l] = create_output_slot<vec3_t>( vec3_t(0) ) ;
            }
            ambient = create_output_slot<float_t>( 0.09f ) ;
            group_pose = create_output_slot<trafo_t>( trafo_t() ) ;
        }
    } ;

    class object_controls : public motor::scene::icomponent
    {
    public:
        output_t<trafo_t> * pose = nullptr ;
        output_t<mat4_t> * normal = nullptr ;
        output_t<vec4_t> * material = nullptr ;

        object_controls( void_t ) noexcept
        {
            pose = create_output_slot<trafo_t>( trafo_t() ) ;
            normal = create_output_slot<mat4_t>( mat4_t::make_identity() ) ;
            material = create_output_slot<vec4_t>( vec4_t(1) ) ;
        }
    } ;

    struct vertex { vec3_t position, normal, color ; } ;

    class scene_wire_app : public motor::application::app
    {
        static constexpr size_t geometry_count = 4 ;
        static constexpr float_t pi = 3.14159265359f ;
        struct object
        {
            vec3_t position, scale, rotation, color ;
            float_t spin, gloss ;
            // All component pointers are borrowed from the scene graph.
            object_controls * controls = nullptr ;
            motor::scene::trafo3d_component_mtr_t trafo = nullptr ;
            motor::scene::msl_component_mtr_t msl = nullptr ;
            size_t subset[2] = {size_t(-1),size_t(-1)} ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t(-1) ;
            float_t aspect = 760.0f / 620.0f ;
            motor::gfx::generic_camera_t camera ;
            size_t frames = 0 ;
            bool_t ready = false ;
            motor::math::vec2ui_t dimensions = motor::math::vec2ui_t(760,620) ;
            motor::graphics::state_object_t present_state ;
            bool_t viewport_dirty = true, post_ready = false ;
            size_t post_frames = 0, direct_frames = 0 ;
        } ;

        options _opts ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _geometry[geometry_count] ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        size_t _links[geometry_count] = {} ;
        motor::graphics::state_object_t _state ;
        motor::graphics::state_object_t _hdr_state ;
        motor::gfx::hdr_postprocess_pipeline_mtr_t _post = nullptr ;
        motor::gfx::hdr_postprocess_pipeline_t::property_sheets_t _post_properties ; // borrowed
        motor::scene::logic_group_mtr_t _root = nullptr ;
        motor::scene::logic_group_mtr_t _exhibits = nullptr ; // borrowed
        lighting_controls * _controls = nullptr ; // borrowed
        motor::vector<object> _objects ;
        window_data _windows[2] ;
        bool_t _enabled[3] = {true,true,true} ;
        float_t _intensity[3] = {6.0f,3.0f,5.0f} ;
        float_t _time = 0, _yaw = 25, _ambient = 0.09f, _specular = 0.22f ;
        float_t _group_height = 0, _tint = 1 ;
        bool_t _paused = false, _quitting = false, _separate_view = false, _scene_ready = false ;
        bool_t _post_enabled = true ;
        size_t _phase = 0, _triangles = 0 ;
        std::chrono::steady_clock::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        bool_t all_ready( void_t ) const noexcept
        { return _windows[0].ready && ( window_count() == 1 || _windows[1].ready ) ; }

        void_t prepare_presentation( size_t const w, motor::graphics::gen4::frontend_ptr_t fe ) noexcept
        {
            auto & window = _windows[w] ;
            if( !window.viewport_dirty ) return ;
            window.present_state.access_render_state(0,[&]( motor::graphics::render_state_sets_ref_t states )
            {
                states.view_s.ss.vp = motor::math::vec4ui_t(0,0,window.dimensions.x(),window.dimensions.y()) ;
                return true ;
            }) ;
            fe->configure<motor::graphics::state_object_t>(&window.present_state) ;
            window.viewport_dirty = false ;
        }

        bool_t verify_postprocess( void_t ) noexcept
        {
            using float_slot_t = motor::wire::input_slot<float_t> ;
            auto * threshold = _post_properties.at("brightpass")->borrow_property<float_slot_t>("brightness_threshold") ;
            auto * tone = _post_properties.at("tone_map")->borrow_property<motor::wire::input_slot<vec3_t>>("reinhard_offset") ;
            auto * merge = _post_properties.at("merge")->borrow_property<float_slot_t>("strength_b") ;
            auto * fxaa = _post_properties.at("fxaa")->borrow_property<float_slot_t>("subpixel_blending") ;
            auto * bloom = _post_properties.at("bloom")->borrow_property<float_t>("upsample_radius") ;
            float_t const expected = _phase == 4 ? 1.5f : (_phase == 5 ? 3.0f : 2.0f) ;
            if( !threshold || !tone || !merge || !fxaa || !bloom ||
                std::abs(threshold->get()-expected) > 0.0001f ) return false ;
            for( size_t w = 0 ; w < window_count() ; ++w )
                if( !_windows[w].post_ready || (_phase == 5 &&
                    (_windows[w].post_frames == 0 || _windows[w].direct_frames == 0)) ) return false ;
            return true ;
        }

        void_t fail( char_cptr_t message ) noexcept
        {
            motor::log::global_t::error(message) ;
            _quitting = true ;
            close() ;
        }

        template< typename T >
        void_t connect_shader( motor::scene::msl_component_mtr_t comp,
            motor::graphics::variable_set_mtr_t set, motor::string_cref_t name,
            output_t<T> * output ) noexcept
        {
            set->data_variable<T>(name.c_str())->set( output->get_value() ) ;
            auto & inputs = comp->borrow_shader_inputs() ;
            inputs.borrow_or_add( name, motor::shared( motor::wire::input_slot<T>(output->get_value()) ) ) ;
            if( !inputs.connect(name, motor::share(output)) )
                fail("[scene wire] incompatible shader-slot connection") ;
        }

        static void_t triangle( motor::vector<vertex> & vertices,
            vertex const & a, vertex const & b, vertex const & c ) noexcept
        {
            vertices.push_back( a ) ; vertices.push_back( b ) ; vertices.push_back( c ) ;
        }

        void_t upload_mesh( size_t const index, motor::string_cref_t name,
            motor::vector<vertex> const & vertices ) noexcept
        {
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element( motor::graphics::vertex_attribute::position,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .add_layout_element( motor::graphics::vertex_attribute::normal,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .add_layout_element( motor::graphics::vertex_attribute::color0,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 ) ;
            vb.resize( vertices.size() ).update<vertex>( [&]( vertex * ptr, size_t const count )
            {
                for( size_t i = 0 ; i < count ; ++i ) ptr[i] = vertices[i] ;
            } ) ;
            auto ib = motor::graphics::index_buffer_t().set_layout_element( motor::graphics::type::tuint ) ;
            ib.resize( vertices.size() ).update<uint_t>( []( uint_t * ptr, size_t const count )
            {
                for( size_t i = 0 ; i < count ; ++i ) ptr[i] = uint_t(i) ;
            } ) ;
            _triangles += vertices.size() / 3 ;
            _geometry[index] = motor::graphics::geometry_object_t( name,
                motor::graphics::primitive_type::triangles, std::move(vb), std::move(ib) ) ;
        }

        void_t make_geometry( void_t ) noexcept
        {
            for( size_t mesh = 0 ; mesh < 2 ; ++mesh )
            {
                motor::geometry::tri_mesh_t source ;
                motor::geometry::cube_t::input_params params ;
                motor::geometry::cube_t::make( &source, params ) ;
                motor::geometry::flat_tri_mesh_t flat ;
                source.flatten( flat ) ;
                motor::vector<vertex> vertices ;
                vertices.reserve( flat.indices.size() * (mesh == 0 ? 1 : 144) ) ;
                for( size_t index = 0 ; index < flat.indices.size() ; index += 3 )
                {
                    if( mesh == 0 )
                    {
                        for( size_t k = 0 ; k < 3 ; ++k )
                        {
                            auto const i = flat.indices[index+k] ;
                            vertices.push_back( { flat.get_vertex_position_3d(i), flat.get_vertex_normal_3d(i), vec3_t(1.0f) } ) ;
                        }
                        continue ;
                    }
                    // The cube generator's triangle overload does not tessellate.
                    auto const a = flat.get_vertex_position_3d(flat.indices[index]) ;
                    auto const u = flat.get_vertex_position_3d(flat.indices[index+1])-a ;
                    auto const v = flat.get_vertex_position_3d(flat.indices[index+2])-a ;
                    auto point = [&]( size_t const x, size_t const y )
                    {
                        auto const n = (a + u*(float_t(x)/12.0f) + v*(float_t(y)/12.0f)).normalized() ;
                        return vertex{n,n,vec3_t(1.0f)} ;
                    } ;
                    for( size_t x = 0 ; x < 12 ; ++x )
                    {
                        for( size_t y = 0 ; y < 12-x ; ++y )
                        {
                            triangle(vertices,point(x,y),point(x+1,y),point(x,y+1)) ;
                            if( x+y+1 < 12 ) triangle(vertices,point(x+1,y),point(x+1,y+1),point(x,y+1)) ;
                        }
                    }
                }
                upload_mesh( mesh, mesh == 0 ? "lit_cube" : "lit_sphere", vertices ) ;
            }
            {
                motor::vector<vertex> vertices ;
                vertices.reserve( 48 * 20 * 6 ) ;
                auto point = []( size_t const u, size_t const v )
                {
                    float_t const a = float_t(u) * 2.0f * pi / 48.0f ;
                    float_t const b = float_t(v) * 2.0f * pi / 20.0f ;
                    vec3_t const n( std::cos(a)*std::cos(b), std::sin(b), std::sin(a)*std::cos(b) ) ;
                    return vertex{ vec3_t( std::cos(a), 0.0f, std::sin(a) ) * 0.85f + n * 0.3f,
                        n, vec3_t(1.0f) } ;
                } ;
                for( size_t u = 0 ; u < 48 ; ++u )
                {
                    for( size_t v = 0 ; v < 20 ; ++v )
                    {
                        auto const a = point(u,v), b = point(u+1,v), c = point(u+1,v+1), d = point(u,v+1) ;
                        triangle( vertices, a, c, b ) ; triangle( vertices, a, d, c ) ;
                    }
                }
                upload_mesh( 2, "lit_torus", vertices ) ;
            }
            {
                motor::vector<vertex> vertices ;
                vertices.reserve( 12 * 10 * 6 ) ;
                for( size_t z = 0 ; z < 10 ; ++z )
                {
                    for( size_t x = 0 ; x < 12 ; ++x )
                    {
                        float_t const px = float_t(x)-6.0f, pz = float_t(z)-5.0f ;
                        vec3_t const n(0,1,0), color( (x+z)%2 == 0 ? 0.72f : 0.44f ) ;
                        vertex const a{vec3_t(px,0,pz),n,color}, b{vec3_t(px+1,0,pz),n,color} ;
                        vertex const c{vec3_t(px+1,0,pz+1),n,color}, d{vec3_t(px,0,pz+1),n,color} ;
                        triangle( vertices, a, c, b ) ; triangle( vertices, a, d, c ) ;
                    }
                }
                upload_mesh( 3, "lit_floor", vertices ) ;
            }
        }


        void_t make_shader( void_t ) noexcept
        {
            motor::string_t code = R"(
                config scene_wire_lighting
                {
                    vertex_shader
                    {
                        in vec3_t pos : position ;
                        in vec3_t nrm : normal ;
                        in vec3_t tint : color0 ;
                        out vec4_t pos : position ;
                        out vec3_t world_pos : texcoord0 ;
                        out vec3_t world_nrm : texcoord1 ;
                        out vec3_t tint : color0 ;
                        mat4_t u_world : world ;
                        mat4_t u_view : view ;
                        mat4_t u_proj : projection ;
                        mat4_t u_normal ;
                        void main()
                        {
                            vec4_t p = u_world * vec4_t( in.pos, 1.0 ) ;
                            out.pos = u_proj * u_view * p ;
                            out.world_pos = p.xyz ;
                            out.world_nrm = ( u_normal * vec4_t( in.nrm, 0.0 ) ).xyz ;
                            out.tint = in.tint ;
                        }
                    }
                    pixel_shader
                    {
                        in vec3_t world_pos : texcoord0 ;
                        in vec3_t world_nrm : texcoord1 ;
                        in vec3_t tint : color0 ;
                        out vec4_t color : color ;
                        vec4_t u_material ;
                        vec3_t u_eye : camera_position ;
                        float_t u_ambient ;
            )" ;
            for( size_t l = 0 ; l < 3 ; ++l )
            {
                auto const suffix = motor::to_string(l) ;
                code += "vec3_t u_direction_" + suffix + " ; vec3_t u_energy_" + suffix + " ;\n" ;
            }
            code += R"(
                        void main()
                        {
                            vec3_t n = normalize( in.world_nrm ) ;
                            vec3_t v = normalize( u_eye - in.world_pos ) ;
                            vec3_t albedo = in.tint ' u_material.xyz ;
                            vec3_t result = albedo * u_ambient ;
            )" ;
            for( size_t l = 0 ; l < 3 ; ++l )
            {
                auto const suffix = motor::to_string(l) ;
                code += "vec3_t l" + suffix + " = normalize( u_direction_" + suffix + " ) ;\n" ;
                code += "float_t d" + suffix + " = max( dot( n, l" + suffix + " ), 0.0 ) ;\n" ;
                code += "float_t s" + suffix + " = 0.0 ;\n" ;
                code += "if( d" + suffix + " > 0.0 ) { s" + suffix +
                    " = pow( max( dot( n, normalize( l" + suffix + " + v ) ), 0.0 ), 48.0 ) * u_material.w ; }\n" ;
                code += "result = result + ( albedo * d" + suffix + " + as_vec3( s" + suffix +
                    " ) ) ' u_energy_" + suffix + " ;\n" ;
            }
            code += "out.color = vec4_t( result, 1.0 ) ; } } }" ;
            motor::graphics::msl_object_t shader("scene_wire_lighting",true) ;
            shader.add(motor::graphics::msl_api_type::msl_4_0,code) ;
            for( size_t g = 0 ; g < geometry_count ; ++g )
                _links[g] = shader.link_geometry(_geometry[g].name()) ;
            _shader = motor::shared(std::move(shader)) ;
        }

        void_t add_object( size_t const geometry, vec3_t const position, vec3_t const scale,
            vec3_t const rotation, vec3_t const color, float_t const spin, float_t const gloss ) noexcept
        {
            object o{} ;
            o.position = position ; o.scale = scale ; o.rotation = rotation ;
            o.color = color ; o.spin = spin ; o.gloss = gloss ;
            auto leaf = motor::shared(motor::scene::logic_leaf_t()) ;
            auto controls = motor::shared(object_controls()) ;
            o.controls = controls ;
            leaf->add_component(motor::move(controls)) ;
            auto tc = motor::shared(motor::scene::trafo3d_component_t()) ;
            o.trafo = tc ;
            if( !tc->borrow_trafo_is()->connect(motor::share(o.controls->pose)) )
                fail("[scene wire] transform-slot connection failed") ;
            leaf->add_component(motor::move(tc)) ;

            auto vars = motor::shared(motor::graphics::variable_set_t()) ;
            vars->data_variable<mat4_t>("u_world")->set(mat4_t::make_identity()) ;
            vars->data_variable<mat4_t>("u_view")->set(mat4_t::make_identity()) ;
            vars->data_variable<mat4_t>("u_proj")->set(mat4_t::make_identity()) ;
            vars->data_variable<vec3_t>("u_eye")->set(vec3_t(0)) ;
            size_t const base = _shader->add_variable_set(motor::share(vars)) ;
            auto comp = motor::shared(motor::scene::msl_component_t(motor::share(_shader),base,_links[geometry])) ;
            o.msl = comp ;
            connect_shader(comp,vars,"u_material",o.controls->material) ;
            connect_shader(comp,vars,"u_normal",o.controls->normal) ;
            connect_shader(comp,vars,"u_ambient",_controls->ambient) ;
            for( size_t l = 0 ; l < 3 ; ++l )
            {
                auto const suffix = motor::to_string(l) ;
                connect_shader(comp,vars,"u_direction_"+suffix,_controls->direction[l]) ;
                connect_shader(comp,vars,"u_energy_"+suffix,_controls->energy[l]) ;
            }
            motor::release(motor::move(vars)) ;
            leaf->add_component(motor::shared(motor::scene::msl_set_component_t(0,motor::move(comp)))) ;
            if( spin != 0 ) _exhibits->add_child(motor::move(leaf)) ;
            else _root->add_child(motor::move(leaf)) ;
            _objects.emplace_back(std::move(o)) ;
        }

        void_t update_scene( void_t ) noexcept
        {
            vec3_t const colors[3] = {{1,0.88f,0.72f},{0.38f,0.64f,1},{0.9f,0.32f,0.24f}} ;
            vec3_t const directions[3] = {
                vec3_t(-0.6f+0.35f*std::sin(_time*0.25f),0.9f,0.65f).normalized(),
                vec3_t(0.9f,0.3f,0.4f).normalized(),vec3_t(0.2f,0.65f,-1).normalized()} ;
            for( size_t l = 0 ; l < 3 ; ++l )
            {
                _controls->direction[l]->set_and_exchange(directions[l]) ;
                _controls->energy[l]->set_and_exchange(colors[l]*(_enabled[l] ? _intensity[l] : 0.0f)) ;
            }
            _controls->ambient->set_and_exchange(_ambient) ;
            _controls->group_pose->set_and_exchange(trafo_t(vec3_t(1),vec3_t(0),vec3_t(0,_group_height,0))) ;
            for( auto & o : _objects )
            {
                auto const rotation = o.rotation+vec3_t(0,o.spin*_time,0) ;
                o.controls->pose->set_and_exchange(trafo_t(o.scale,rotation,o.position)) ;
                // The parent only translates, so this is also the world normal transform.
                o.controls->normal->set_and_exchange(trafo_t(
                    vec3_t(1.0f/o.scale.x(),1.0f/o.scale.y(),1.0f/o.scale.z()),rotation,vec3_t(0)).get_transformation()) ;
                o.controls->material->set_and_exchange(vec4_t(o.color*_tint,o.gloss*_specular)) ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                float_t const yaw = (_yaw+(w == 1 && _separate_view ? 35.0f : 0.0f))*pi/180.0f ;
                auto & cam = _windows[w].camera ;
                cam.make_perspective_fov(_windows[w].aspect,1.0f,0.8f,0.1f,80.0f) ;
                cam.look_at(vec3_t(18*std::sin(yaw),9,18*std::cos(yaw)),vec3_t(0,1,0),vec3_t(0,1,0)) ;
            }
            _scene_ready = all_ready() ;
            if( _scene_ready )
            {
                // Process compilation listeners before the transform visitor publishes world.
                for( auto & o : _objects ) o.msl->render_update(&_windows[0].camera) ;
            }
            {
                motor::scene::variable_update_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply(&visitor) ;
            }
            {
                motor::scene::trafo_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply(&visitor) ;
            }
        }

        template< typename T >
        static bool_t matches( motor::graphics::variable_set_mtr_t set,
            char_cptr_t name, T const & expected ) noexcept
        {
            motor::graphics::data_variable<T> * var = nullptr ;
            set->for_each_data_variable([&]( motor::string_in_t key, motor::graphics::ivariable_ptr_t value )
            {
                if( key == name ) var = dynamic_cast<motor::graphics::data_variable<T> *>(value) ;
            }) ;
            bool_t equal = var != nullptr ;
            if( var != nullptr )
            {
                if constexpr( std::is_same_v<T,float_t> )
                    equal = std::isfinite(var->get()) && std::abs(var->get()-expected) < 0.0001f ;
                else
                {
                    constexpr size_t count = std::is_same_v<T,mat4_t> ? 16 : (std::is_same_v<T,vec4_t> ? 4 : 3) ;
                    for( size_t k = 0 ; k < count ; ++k )
                        equal = equal && std::isfinite(var->get()[k]) && std::abs(var->get()[k]-expected[k]) < 0.0001f ;
                }
            }
            if( !equal ) motor::log::global_t::error(motor::string_t("[scene wire] variable mismatch: ")+name) ;
            return equal ;
        }

        bool_t verify_frame( void_t ) noexcept
        {
            size_t active_sets = 0 ;
            for( auto const & entry : _shader->borrow_varibale_sets() )
                if( entry.vs != nullptr ) ++active_sets ;
            if( active_sets != _objects.size()*(1+window_count()) ) return false ;
            for( auto const & o : _objects )
            {
                auto const local = o.controls->pose->get_value().get_transformation() ;
                auto const expected = o.spin != 0 ?
                    _controls->group_pose->get_value().get_transformation()*local : local ;
                auto const & actual = o.trafo->get_computed().get_transformation() ;
                for( size_t k = 0 ; k < 16 ; ++k )
                    if( std::abs(actual[k]-expected[k]) > 0.0001f ) return false ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    if( o.subset[w] == size_t(-1) ) return false ;
                    auto const borrowed = _shader->borrow_varibale_set(o.subset[w]) ;
                    auto * set = borrowed.vs ;
                    if( set == nullptr ) return false ;
                    if( !matches(set,"u_world",actual) ||
                        !matches(set,"u_view",_windows[w].camera.get_view_matrix()) ||
                        !matches(set,"u_proj",_windows[w].camera.get_proj_matrix()) ||
                        !matches(set,"u_eye",_windows[w].camera.get_position()) ||
                        !matches(set,"u_material",o.controls->material->get_value()) ||
                        !matches(set,"u_normal",o.controls->normal->get_value()) ||
                        !matches(set,"u_ambient",_controls->ambient->get_value()) ) return false ;
                    for( size_t l = 0 ; l < 3 ; ++l )
                    {
                        auto const suffix = motor::to_string(l) ;
                        if( !matches(set,("u_energy_"+suffix).c_str(),_controls->energy[l]->get_value()) ||
                            !matches(set,("u_direction_"+suffix).c_str(),_controls->direction[l]->get_value()) )
                            return false ;
                    }
                }
                if( window_count() == 2 && o.subset[0] == o.subset[1] ) return false ;
            }
            return true ;
        }

    public:
        scene_wire_app( options const opts, bool_t * passed ) noexcept :
            _opts(opts), _passed(passed), _paused(opts.still) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            {
                // Construct in place; keep the pipeline alive through all backend releases.
                _post = motor::memory::global_t::create<motor::gfx::hdr_postprocess_pipeline_t>() ;
                _post->init() ;
                _post_properties = _post->property_sheets() ;
                auto * threshold = _post_properties.at("brightpass")->borrow_property<motor::wire::input_slot<float_t>>("brightness_threshold") ;
                if( threshold ) threshold->set(2.0f) ;
                else fail("[scene wire] missing bright-pass property") ;
            }
            make_geometry() ;
            make_shader() ;
            _root = motor::shared(motor::scene::logic_group_t()) ;
            {
                auto controls = motor::shared(lighting_controls()) ;
                _controls = controls ;
                _root->add_component(motor::move(controls)) ;
                auto group = motor::shared(motor::scene::logic_group_t()) ;
                _exhibits = group ;
                auto tc = motor::shared(motor::scene::trafo3d_component_t()) ;
                if( !tc->borrow_trafo_is()->connect(motor::share(_controls->group_pose)) )
                    fail("[scene wire] parent transform connection failed") ;
                group->add_component(motor::move(tc)) ;
                _root->add_child(motor::move(group)) ;
            }
            {
                add_object( 3, vec3_t(0), vec3_t(1), vec3_t(0), vec3_t(0.55f,0.59f,0.62f), 0, 0.15f ) ;
                for( size_t i = 0 ; i < 3 ; ++i )
                {
                    float_t const x = (float_t(i)-1.0f)*3.2f ;
                    add_object( 0, vec3_t(x,0.35f,-0.7f), vec3_t(2.55f,0.7f,2.55f), vec3_t(0),
                        vec3_t(0.3f,0.34f,0.38f), 0, 0.3f ) ;
                }
                add_object( 1, vec3_t(-3.2f,1.8f,-0.7f), vec3_t(1.1f), vec3_t(0), vec3_t(0.18f,0.72f,0.52f), 0.25f, 1 ) ;
                add_object( 2, vec3_t(0,2.0f,-0.7f), vec3_t(1.05f), vec3_t(pi*0.5f,0,0), vec3_t(0.86f,0.61f,0.18f), 0.3f, 1 ) ;
                add_object( 0, vec3_t(3.2f,1.85f,-0.7f), vec3_t(1.65f), vec3_t(0.25f,0.4f,0.1f), vec3_t(0.25f,0.48f,0.92f), 0.25f, 0.8f ) ;
                add_object( 1, vec3_t(-2.0f,0.55f,2.4f), vec3_t(0.75f,0.55f,0.5f), vec3_t(0), vec3_t(0.83f,0.26f,0.22f), 0.2f, 0.7f ) ;
                add_object( 2, vec3_t(0.4f,0.33f,2.5f), vec3_t(1.0f), vec3_t(0), vec3_t(0.63f,0.65f,0.7f), -0.2f, 1.0f ) ;
                add_object( 0, vec3_t(3.2f,0.6f,2.5f), vec3_t(0.75f,1.2f,0.75f), vec3_t(0,0.5f,0), vec3_t(0.78f,0.74f,0.62f), 0, 0.2f ) ;
                add_object( 0, vec3_t(0,1.8f,-4.7f), vec3_t(12.0f,3.6f,0.35f), vec3_t(0), vec3_t(0.32f,0.36f,0.4f), 0, 0 ) ;
                add_object( 0, vec3_t(-5.7f,0.8f,-1.0f), vec3_t(0.4f,1.6f,7.0f), vec3_t(0), vec3_t(0.4f,0.44f,0.47f), 0, 0.1f ) ;
            }

            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.blend_s.do_change = true ;
                states.blend_s.ss.do_activate = false ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = vec4_t(0.055f,0.065f,0.08f,1) ;
                _state = motor::graphics::state_object_t("scene_wire_state") ;
                _state.add_render_state_set(states) ;

                // A single forward pass writes color AND depth; no separate Z prepass.
                states.view_s.do_change = true ;
                states.view_s.ss.do_activate = true ;
                states.view_s.ss.vp = motor::math::vec4ui_t(0,0,1920,1080) ;
                _hdr_state = motor::graphics::state_object_t("scene_wire_hdr_state") ;
                _hdr_state.add_render_state_set(states) ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                {
                    motor::graphics::render_state_sets_t states ;
                    states.depth_s.do_change = true ;
                    states.depth_s.ss.do_activate = false ;
                    states.depth_s.ss.do_depth_write = false ;
                    states.polygon_s.do_change = true ;
                    states.polygon_s.ss.do_activate = false ;
                    states.blend_s.do_change = true ;
                    states.blend_s.ss.do_activate = false ;
                    states.clear_s.do_change = true ;
                    states.clear_s.ss.do_activate = false ;
                    states.view_s.do_change = true ;
                    states.view_s.ss.do_activate = true ;
                    states.view_s.ss.vp = motor::math::vec4ui_t(0,0,760,620) ;
                    _windows[w].present_state = motor::graphics::state_object_t("scene_wire_present_"+motor::to_string(w)) ;
                    _windows[w].present_state.add_render_state_set(states) ;
                }
                motor::application::window_info_t wi ;
                bool_t const d3d = _opts.d3d_only || w == 1 ;
                wi.window_name = d3d ? "Motor | scene + wire + HDR | D3D11" : "Motor | scene + wire + HDR | GL4" ;
                wi.x = int_t(30+w*780) ; wi.y = 60 ; wi.w = 760 ; wi.h = 620 ;
                wi.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 :
                    motor::application::graphics_generation::gen4_gl4 ;
                _windows[w].id = create_window(wi) ;
                send_window_message(_windows[w].id,[]( auto & window )
                {
                    window.send_message(motor::application::show_message({true})) ;
                    window.send_message(motor::application::vsync_message_t({true})) ;
                }) ;
            }
            update_scene() ;
            motor::log::global_t::status<256>("[scene wire] %zu objects, %zu unique triangles, 3 lights, one shared graph",
                _objects.size(),_triangles) ;
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused ) _time += data.sec_dt ;
            update_scene() ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            size_t const w = id == _windows[0].id ? 0 : 1 ;
            if( data.last_frame )
            {
                _post->release_render(fe) ;
                fe->release<motor::graphics::state_object_t>(&_windows[w].present_state) ;
                fe->release<motor::graphics::state_object_t>(&_hdr_state) ;
                fe->release<motor::graphics::msl_object_t>(_shader) ;
                for( auto & geo : _geometry ) fe->release<motor::graphics::geometry_object_t>(&geo) ;
                fe->release<motor::graphics::state_object_t>(&_state) ;
                return ;
            }
            if( data.first_frame )
            {
                for( auto & geo : _geometry ) fe->configure<motor::graphics::geometry_object_t>(&geo) ;
                fe->configure<motor::graphics::state_object_t>(&_state) ;
                fe->configure<motor::graphics::state_object_t>(&_hdr_state) ;
                fe->configure<motor::graphics::msl_object_t>(_shader) ;
                _post->init_render(fe) ;
            }
            prepare_presentation(w,fe) ;
            auto const status = fe->decode(_shader) ;
            _windows[w].ready = status.first == motor::graphics::object_state::ready &&
                status.second == motor::graphics::result::ok ;
            auto const post_status = fe->decode(_post->borrow_hdr_fb(0)) ;
            _windows[w].post_ready = post_status.first == motor::graphics::object_state::ready &&
                post_status.second == motor::graphics::result::ok ;
            bool_t const use_post = _post_enabled && _windows[w].post_ready ;
            if( use_post ) fe->use(_post->borrow_hdr_fb(0)) ;
            fe->push(use_post ? &_hdr_state : &_state) ;
            if( _scene_ready && all_ready() && !_quitting )
            {
                if( _opts.smoke )
                {
                    // Resolve test IDs before queuing any draws, never mutate submitted sets.
                    for( auto & o : _objects )
                        o.subset[w] = o.msl->render_update(w,&_windows[w].camera) ;
                }
                motor::scene::render_visitor_t visitor(0,w,fe,&_windows[w].camera) ;
                motor::scene::node_t::traverser(_root).apply(&visitor) ;
                ++_windows[w].frames ;
                if( use_post ) ++_windows[w].post_frames ;
                else ++_windows[w].direct_frames ;
            }
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            if( use_post )
            {
                fe->unuse(motor::graphics::gen4::backend::unuse_type::framebuffer) ;
                // Fullscreen stages inherit disabled depth/blending and the window viewport.
                fe->push(&_windows[w].present_state) ;
                _post->render(fe) ;
                fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            }
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos(ImVec2(12,44),ImGuiCond_FirstUseEver) ;
            ImGui::SetNextWindowSize(ImVec2(300,440),ImGuiCond_FirstUseEver) ;
            if( ImGui::Begin("Scene + Wire",nullptr,ImGuiWindowFlags_NoSavedSettings) )
            {
                ImGui::BeginDisabled(_opts.smoke) ;
                ImGui::Checkbox("Key",&_enabled[0]) ;
                ImGui::SliderFloat("Key intensity",&_intensity[0],0.0f,12.0f) ;
                ImGui::Checkbox("Fill",&_enabled[1]) ;
                ImGui::SliderFloat("Fill intensity",&_intensity[1],0.0f,12.0f) ;
                ImGui::Checkbox("Rim",&_enabled[2]) ;
                ImGui::SliderFloat("Rim intensity",&_intensity[2],0.0f,12.0f) ;
                ImGui::SliderFloat("Ambient",&_ambient,0.0f,0.3f) ;
                ImGui::SliderFloat("Material tint",&_tint,0.2f,1.0f) ;
                ImGui::SliderFloat("Exhibits height",&_group_height,0.0f,2.0f) ;
                ImGui::SliderFloat("Camera",&_yaw,-70.0f,70.0f) ;
                ImGui::Checkbox("Second viewpoint",&_separate_view) ;
                ImGui::Checkbox("Pause",&_paused) ;
                ImGui::Separator() ;
                ImGui::Checkbox("HDR post processing",&_post_enabled) ;
                if( ImGui::CollapsingHeader("Post processing") )
                {
                    bool_t changed = false ;
                    for( auto const & name : {"brightpass","bloom","merge","tone_map","fxaa"} )
                        changed |= motor::tool::imgui_property::handle(name,*_post_properties.at(name)) ;
                    if( changed ) _post->update_properies() ;
                }
                ImGui::EndDisabled() ;
            }
            ImGui::End() ;
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w )
                if( _windows[w].frames < 5 ) return ;
            if( !verify_frame() )
            {
                fail("[scene wire] hierarchy / bridge / camera subset verification failed") ;
                return ;
            }
            if( !verify_postprocess() )
            {
                fail("[scene wire] HDR framebuffer / stage property verification failed") ;
                return ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
                if( _windows[w].frames < 45 ) return ;
            motor::log::global_t::status<256>("[scene wire] phase %zu passed: hierarchy, bridges, camera subsets, HDR properties (%s)",
                _phase,_post_enabled ? "post processing" : "direct") ;
            if( ++_phase == 6 )
            {
                *_passed = true ;
                _quitting = true ;
                close() ;
                return ;
            }
            size_t const masks[6] = {7,3,1,0,7,7} ;
            for( size_t l = 0 ; l < 3 ; ++l ) _enabled[l] = (masks[_phase] & (size_t(1)<<l)) != 0 ;
            _tint = _phase == 4 ? 0.45f : 1.0f ;
            _group_height = _phase >= 4 ? 1.2f : 0.0f ;
            _separate_view = _phase >= 4 ;
            _post_enabled = _phase != 3 ;
            _post_properties.at("brightpass")->borrow_property<motor::wire::input_slot<float_t>>("brightness_threshold")
                ->set(_phase == 4 ? 1.5f : (_phase == 5 ? 3.0f : 2.0f)) ;
            if( _phase == 5 )
            {
                _paused = true ;
                _ambient = 0.16f ;
                _intensity[1] = 0.8f ;
            }
            for( auto & window : _windows ) window.frames = 0 ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting &&
                std::chrono::steady_clock::now()-_started > std::chrono::seconds(90) )
                fail("[scene wire] smoke timeout") ;
        }

        void_t on_event( window_id_t const id,
            motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        {
            if( s.resize_changed && s.resize_msg.resize && s.resize_msg.w > 0 && s.resize_msg.h > 0 )
            {
                auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
                window.aspect = float_t(s.resize_msg.w)/float_t(s.resize_msg.h) ;
                window.dimensions = motor::math::vec2ui_t(uint_t(s.resize_msg.w),uint_t(s.resize_msg.h)) ;
                window.viewport_dirty = true ;
            }
            if( s.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            _objects.clear() ;
            _controls = nullptr ;
            _exhibits = nullptr ;
            // Components disconnect their slots before releasing them.
            motor::release(motor::move(_root)) ;
            motor::release(motor::move(_shader)) ;
            _post_properties.clear() ;
            motor::release(motor::move(_post)) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
#ifndef _WIN32
    opts.dual = false ;
#endif
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d_only = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d_only = true ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else
        {
            motor::log::global_t::status("Usage: 12_scene_wire_lighting [--gl-only|--d3d-only] [--smoke] [--still]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.d3d_only ) { motor::log::global_t::error("D3D11 requires Windows.") ; motor::log::global_t::deinit() ; return 2 ; }
#endif
    bool passed = false ; int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::scene_wire_app(opts,&passed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const remaining = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : (remaining != 0 || (opts.smoke && !passed) ? 1 : 0) ;
}
