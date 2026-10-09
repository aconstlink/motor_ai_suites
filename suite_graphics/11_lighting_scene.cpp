#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/geometry/3d/cube.h>
#include <motor/geometry/mesh/tri_mesh.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/string>
#include <motor/std/vector>

#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using mat4_t = motor::math::mat4f_t ;
    template< typename T > using variable_t = motor::graphics::data_variable<T> ;

    struct options
    {
        bool_t dual = true, d3d_only = false, smoke = false, still = false ;
    } ;

    struct vertex
    {
        vec3_t position, normal, color ;
    } ;

    class lighting_scene_app : public motor::application::app
    {
        // Shaders: ambient-only, one light, three lights. Bindings: ambient,
        // combined, key, fill, rim. Every window/draw owns stable variable data.
        static constexpr size_t shader_count = 3, geometry_count = 4 ;
        static constexpr float_t pi = 3.14159265359f ;
        struct binding
        {
            size_t id = size_t(-1) ;
            variable_t<mat4_t> * world = nullptr, * normal = nullptr, * vp = nullptr ;
            variable_t<vec4_t> * material = nullptr ;
            variable_t<vec3_t> * eye = nullptr ;
            variable_t<float_t> * ambient = nullptr ;
            variable_t<vec3_t> * direction[3] = {}, * color[3] = {} ;
        } ;
        struct object
        {
            size_t geometry ;
            vec3_t position, scale, rotation, color ;
            float_t spin, gloss ;
            binding sets[2][5] ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t(-1) ;
            float_t aspect = 760.0f / 620.0f ;
            motor::gfx::generic_camera_t camera ;
            size_t frames = 0, submissions = 0 ;
        } ;

        options _opts ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _geometry[geometry_count] ;
        motor::graphics::msl_object_mtr_t _shaders[shader_count] = {} ;
        size_t _links[shader_count][geometry_count] = {} ;
        motor::graphics::state_object_t _opaque, _additive ;
        motor::vector<object> _objects ;
        window_data _windows[2] ;
        bool_t _enabled[3] = { true, true, true } ;
        float_t _intensity[3] = { 0.65f, 0.45f, 0.6f } ;
        float_t _time = 0.0f, _yaw = 25.0f, _ambient = 0.09f, _specular = 0.22f ;
        bool_t _paused = false, _quitting = false ;
        int_t _mode = 0, _frame_mode = 0 ;
        size_t _frame_mask = 7, _phase = 0, _triangles = 0 ;
        std::chrono::steady_clock::time_point _started ;

        size_t window_count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        static size_t shader_for_binding( size_t const b ) noexcept
        { return b == 0 ? 0 : ( b == 1 ? 2 : 1 ) ; }

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

        void_t make_shaders( void_t ) noexcept
        {
            for( size_t s = 0 ; s < shader_count ; ++s )
            {
                size_t const lights = s == 0 ? 0 : ( s == 1 ? 1 : 3 ) ;
                motor::string_t const name = "lighting_scene_" + motor::to_string(s) ;
                motor::string_t code = "config " + name + R"(
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
                        mat4_t u_world ; mat4_t u_normal ; mat4_t u_vp ;
                        void main()
                        {
                            vec4_t p = u_world * vec4_t( in.pos, 1.0 ) ;
                            out.pos = u_vp * p ;
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
                        vec3_t u_eye ;
                        float_t u_ambient ;
                )" ;
                for( size_t l = 0 ; l < lights ; ++l )
                {
                    auto const suffix = motor::to_string(l) ;
                    code += "vec3_t u_direction_" + suffix + " ; vec3_t u_light_color_" + suffix + " ;\n" ;
                }
                code += R"(
                        void main()
                        {
                            vec3_t n = normalize( in.world_nrm ) ;
                            vec3_t v = normalize( u_eye - in.world_pos ) ;
                            vec3_t albedo = in.tint ' u_material.xyz ;
                            vec3_t result = albedo * u_ambient ;
                )" ;
                for( size_t l = 0 ; l < lights ; ++l )
                {
                    auto const suffix = motor::to_string(l) ;
                    code += "vec3_t l" + suffix + " = normalize( u_direction_" + suffix + " ) ;\n" ;
                    code += "float_t d" + suffix + " = max( dot( n, l" + suffix + " ), 0.0 ) ;\n" ;
                    code += "float_t s" + suffix + " = 0.0 ;\n" ;
                    code += "if( d" + suffix + " > 0.0 ) { s" + suffix +
                        " = pow( max( dot( n, normalize( l" + suffix + " + v ) ), 0.0 ), 48.0 ) * u_material.w ; }\n" ;
                    code += "result = result + ( albedo * d" + suffix + " + as_vec3( s" + suffix +
                        " ) ) ' u_light_color_" + suffix + " ;\n" ;
                }
                code += "out.color = vec4_t( result, 1.0 ) ; } } }" ;
                motor::graphics::msl_object_t shader( name ) ;
                shader.add( motor::graphics::msl_api_type::msl_4_0, code ) ;
                for( size_t g = 0 ; g < geometry_count ; ++g ) _links[s][g] = shader.link_geometry( _geometry[g].name() ) ;
                _shaders[s] = motor::shared( std::move(shader) ) ;
            }
        }

        void_t add_object( size_t const geometry, vec3_t const position, vec3_t const scale,
            vec3_t const rotation, vec3_t const color, float_t const spin, float_t const gloss ) noexcept
        {
            object o{} ;
            o.geometry = geometry ; o.position = position ; o.scale = scale ;
            o.rotation = rotation ; o.color = color ; o.spin = spin ; o.gloss = gloss ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                for( size_t b = 0 ; b < 5 ; ++b )
                {
                    auto & vars = o.sets[w][b] ;
                    auto set = motor::shared( motor::graphics::variable_set_t() ) ;
                    vars.world = set->data_variable<mat4_t>( "u_world" ) ;
                    vars.normal = set->data_variable<mat4_t>( "u_normal" ) ;
                    vars.vp = set->data_variable<mat4_t>( "u_vp" ) ;
                    vars.eye = set->data_variable<vec3_t>( "u_eye" ) ;
                    vars.material = set->data_variable<vec4_t>( "u_material" ) ;
                    vars.ambient = set->data_variable<float_t>( "u_ambient" ) ;
                    size_t const light_count = b == 0 ? 0 : ( b == 1 ? 3 : 1 ) ;
                    for( size_t l = 0 ; l < light_count ; ++l )
                    {
                        auto const suffix = motor::to_string(l) ;
                        vars.direction[l] = set->data_variable<vec3_t>( "u_direction_" + suffix ) ;
                        vars.color[l] = set->data_variable<vec3_t>( "u_light_color_" + suffix ) ;
                    }
                    vars.id = _shaders[shader_for_binding(b)]->add_variable_set( motor::move(set) ) ;
                }
            }
            _objects.emplace_back( std::move(o) ) ;
        }

        void_t update_variables( void_t ) noexcept
        {
            _frame_mode = _mode ; _frame_mask = 0 ;
            vec3_t const colors[3] = { {1.0f,0.88f,0.72f}, {0.38f,0.64f,1.0f}, {0.9f,0.32f,0.24f} } ;
            float_t const a = _time * 0.25f ;
            vec3_t const directions[3] = {
                vec3_t( -0.6f+0.35f*std::sin(a), 0.9f, 0.65f ).normalized(),
                vec3_t( 0.9f, 0.3f, 0.4f ).normalized(), vec3_t( 0.2f, 0.65f, -1.0f ).normalized() } ;
            for( size_t l = 0 ; l < 3 ; ++l ) if( _enabled[l] ) _frame_mask |= size_t(1) << l ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto & window = _windows[w] ;
                float_t const yaw = _yaw * pi / 180.0f ;
                vec3_t const eye( 18.0f*std::sin(yaw), 9.0f, 18.0f*std::cos(yaw) ) ;
                window.camera.make_perspective_fov( window.aspect, 1.0f, 0.8f, 0.1f, 80.0f ) ;
                window.camera.look_at( eye, vec3_t(0,1,0), vec3_t(0,1,0) ) ;
                auto const vp = window.camera.get_proj_matrix() * window.camera.get_view_matrix() ;
                for( auto & o : _objects )
                {
                    vec3_t const rotation = o.rotation + vec3_t(0, o.spin*_time, 0) ;
                    auto const world = motor::math::m3d::trafof_t( o.scale, rotation, o.position ).get_transformation() ;
                    // For world = R*S, normal transform is R*inverse(S), without translation.
                    auto const normal = motor::math::m3d::trafof_t(
                        vec3_t(1.0f/o.scale.x(),1.0f/o.scale.y(),1.0f/o.scale.z()), rotation, vec3_t(0.0f) ).get_transformation() ;
                    for( size_t b = 0 ; b < 5 ; ++b )
                    {
                        auto & vars = o.sets[w][b] ;
                        vars.world->set(world) ; vars.normal->set(normal) ; vars.vp->set(vp) ; vars.eye->set(eye) ;
                        vars.material->set( vec4_t(o.color, o.gloss*_specular) ) ;
                        vars.ambient->set( b < 2 ? _ambient : 0.0f ) ;
                        size_t const light_count = b == 0 ? 0 : ( b == 1 ? 3 : 1 ) ;
                        for( size_t l = 0 ; l < light_count ; ++l )
                        {
                            size_t const source = b == 1 ? l : b-2 ;
                            vars.direction[l]->set( directions[source] ) ;
                            vars.color[l]->set( colors[source] * ( _enabled[source] ? _intensity[source] : 0.0f ) ) ;
                        }
                    }
                }
            }
        }

        void_t draw_objects( size_t const w, size_t const b, motor::graphics::gen4::frontend_ptr_t fe ) noexcept
        {
            size_t const s = shader_for_binding(b) ;
            for( auto const & o : _objects )
            {
                motor::graphics::gen4::backend_t::render_detail_t detail ;
                detail.geo = _links[s][o.geometry] ; detail.varset = o.sets[w][b].id ;
                fe->render( _shaders[s], detail ) ;
                ++_windows[w].submissions ;
            }
        }

    public:
        lighting_scene_app( options const opts, bool_t * passed ) noexcept :
            _opts(opts), _passed(passed), _paused(opts.still) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            make_geometry() ; make_shaders() ;
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
                states.depth_s.do_change = true ; states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ; states.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                states.polygon_s.do_change = true ; states.polygon_s.ss.do_activate = false ;
                states.blend_s.do_change = true ; states.blend_s.ss.do_activate = false ;
                states.clear_s.do_change = true ; states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ; states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = vec4_t(0.055f,0.065f,0.08f,1) ;
                _opaque = motor::graphics::state_object_t("lighting_opaque") ;
                _opaque.add_render_state_set(states) ;
                states.depth_s.ss.do_depth_write = false ;
                states.clear_s.ss.do_activate = false ; states.clear_s.ss.do_color_clear = false ; states.clear_s.ss.do_depth_clear = false ;
                states.blend_s.ss.do_activate = true ;
                states.blend_s.ss.blend_func = motor::graphics::blend_function::add ;
                states.blend_s.ss.src_blend_factor = motor::graphics::blend_factor::one ;
                states.blend_s.ss.dst_blend_factor = motor::graphics::blend_factor::one ;
                _additive = motor::graphics::state_object_t("lighting_additive") ;
                _additive.add_render_state_set(states) ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                motor::application::window_info_t wi ;
                bool_t const d3d = _opts.d3d_only || w == 1 ;
                wi.window_name = d3d ? "Motor | lighting gallery | D3D11" : "Motor | lighting gallery | GL4" ;
                wi.x = int_t(30+w*780) ; wi.y = 60 ; wi.w = 760 ; wi.h = 620 ;
                wi.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                _windows[w].id = create_window(wi) ;
                send_window_message( _windows[w].id, []( auto & window )
                {
                    window.send_message( motor::application::show_message({true}) ) ;
                    window.send_message( motor::application::vsync_message_t({true}) ) ;
                } ) ;
            }
            update_variables() ;
            motor::log::global_t::status<256>( "[lighting scene] %zu objects, 4 linked meshes (%zu unique triangles), 3 directional lights",
                _objects.size(), _triangles ) ;
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused ) _time += data.sec_dt ;
            update_variables() ;
        }

        void_t on_first_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto & geo : _geometry ) fe->configure<motor::graphics::geometry_object_t>(&geo) ;
            fe->configure<motor::graphics::state_object_t>(&_opaque) ;
            fe->configure<motor::graphics::state_object_t>(&_additive) ;
            for( auto shader : _shaders ) fe->configure<motor::graphics::msl_object_t>(shader) ;
        }

        void_t on_last_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto shader : _shaders ) fe->release<motor::graphics::msl_object_t>(shader) ;
            for( auto & geo : _geometry ) fe->release<motor::graphics::geometry_object_t>(&geo) ;
            fe->release<motor::graphics::state_object_t>(&_additive) ;
            fe->release<motor::graphics::state_object_t>(&_opaque) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            size_t const w = id == _windows[0].id ? 0 : 1 ;
            bool_t ready = true ;
            for( auto shader : _shaders )
            {
                auto const status = fe->decode(shader) ;
                ready = ready && status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
            }
            fe->push(&_opaque) ;
            if( ready && !_quitting )
            {
                draw_objects( w, _frame_mode == 0 ? 1 : 0, fe ) ;
                if( _frame_mode == 1 )
                {
                    // Fill depth for the entire scene before accumulating lights.
                    fe->push(&_additive) ;
                    for( size_t l = 0 ; l < 3 ; ++l )
                        if( (_frame_mask & (size_t(1)<<l)) != 0 ) draw_objects(w,l+2,fe) ;
                    fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
                }
                ++_windows[w].frames ;
            }
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            ImGui::SetNextWindowPos(ImVec2(12,44),ImGuiCond_FirstUseEver) ;
            ImGui::SetNextWindowSize(ImVec2(270,310),ImGuiCond_FirstUseEver) ;
            if( ImGui::Begin("Lighting",nullptr,ImGuiWindowFlags_NoSavedSettings) )
            {
                ImGui::BeginDisabled(_opts.smoke) ;
                ImGui::Combo("Passes",&_mode,"Single pass\0Multipass\0") ;
                ImGui::Checkbox("Key",&_enabled[0]) ; ImGui::SameLine() ; ImGui::SliderFloat("##key",&_intensity[0],0.0f,1.0f) ;
                ImGui::Checkbox("Fill",&_enabled[1]) ; ImGui::SameLine() ; ImGui::SliderFloat("##fill",&_intensity[1],0.0f,1.0f) ;
                ImGui::Checkbox("Rim",&_enabled[2]) ; ImGui::SameLine() ; ImGui::SliderFloat("##rim",&_intensity[2],0.0f,1.0f) ;
                ImGui::SliderFloat("Ambient",&_ambient,0.0f,0.3f) ;
                ImGui::SliderFloat("Specular",&_specular,0.0f,0.6f) ;
                ImGui::SliderFloat("Camera",&_yaw,-70.0f,70.0f) ;
                ImGui::Checkbox("Pause",&_paused) ;
                ImGui::EndDisabled() ;
                size_t passes = 1 ;
                if( _frame_mode == 1 ) for( size_t l = 0 ; l < 3 ; ++l ) if( _frame_mask & (size_t(1)<<l) ) ++passes ;
                ImGui::Text("%zu objects | %zu draws/window",_objects.size(),_objects.size()*passes) ;
            }
            ImGui::End() ; return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w ) if( _windows[w].frames < 45 ) return ;
            size_t passes = 1 ;
            if( _frame_mode == 1 ) for( size_t l = 0 ; l < 3 ; ++l ) if( _frame_mask & (size_t(1)<<l) ) ++passes ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                if( _windows[w].submissions != _windows[w].frames*_objects.size()*passes )
                {
                    motor::log::global_t::error("[lighting scene] invalid submission count") ;
                    _quitting = true ; close() ; return ;
                }
            }
            motor::log::global_t::status<256>( "[lighting scene] phase %zu passed: mode=%d mask=%zu, %zu draws/window/frame",
                _phase, _frame_mode, _frame_mask, _objects.size()*passes ) ;
            if( ++_phase == 10 )
            {
                *_passed = true ; _quitting = true ; close() ; return ;
            }
            size_t const masks[10] = {7,7,3,3,1,1,0,0,7,7} ;
            _mode = int_t(_phase%2) ;
            for( size_t l = 0 ; l < 3 ; ++l ) _enabled[l] = (masks[_phase] & (size_t(1)<<l)) != 0 ;
            if( _phase == 8 ) { _yaw = -35.0f ; _specular = 0.5f ; }
            for( auto & window : _windows ) { window.frames = 0 ; window.submissions = 0 ; }
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting && std::chrono::steady_clock::now()-_started > std::chrono::seconds(90) )
            {
                motor::log::global_t::error("[lighting scene] smoke timeout") ;
                _quitting = true ; close() ;
            }
        }
        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        {
            if( s.resize_changed && s.resize_msg.resize && s.resize_msg.w > 0 && s.resize_msg.h > 0 )
                _windows[id==_windows[0].id ? 0 : 1].aspect = float_t(s.resize_msg.w)/float_t(s.resize_msg.h) ;
            if( s.close_changed ) { _quitting = true ; close() ; }
        }
        void_t on_shutdown( void_t ) noexcept override
        {
            _objects.clear() ;
            for( auto & shader : _shaders ) motor::release(motor::move(shader)) ;
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
            motor::log::global_t::status("Usage: 11_lighting_scene [--gl-only|--d3d-only] [--smoke] [--still]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.d3d_only ) { motor::log::global_t::error("D3D11 requires Windows.") ; motor::log::global_t::deinit() ; return 2 ; }
#endif
    bool passed = false ; int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::lighting_scene_app(opts,&passed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const remaining = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : (remaining != 0 || (opts.smoke && !passed) ? 1 : 0) ;
}
