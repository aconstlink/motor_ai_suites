#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/scene/node/logic_group.h>
#include <motor/scene/node/logic_leaf.h>
#include <motor/scene/component/light_component.hpp>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/graphics/msl_set_component.hpp>
#include <motor/scene/visitor/light_collector_visitor.hpp>
#include <motor/scene/visitor/variable_update_visitor.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/scene/visitor/graphics/light_pass_render_visitor.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace sample
{
    using namespace motor::core::types ;
    using vec3_t = motor::math::vec3f_t ;
    using vec4_t = motor::math::vec4f_t ;
    using mat4_t = motor::math::mat4f_t ;
    using trafo_t = motor::math::m3d::trafof_t ;
    using frontend_t = motor::graphics::gen4::frontend_ptr_t ;
    using clock_t = std::chrono::steady_clock ;

    struct options { bool_t dual = true, d3d = false, smoke = false, still = false ; } ;
    struct vertex { vec3_t position, normal ; } ;

    class light_app : public motor::application::app
    {
        static constexpr size_t light_count = 3 ;
        struct object
        {
            // Borrowed from the graph. Subset IDs belong to the associated shader.
            motor::scene::msl_component_mtr_t base = nullptr, lit = nullptr ;
            motor::scene::trafo3d_component_mtr_t trafo = nullptr ;
            size_t subsets[2][light_count] = {} ;
        } ;
        struct window_data
        {
            window_id_t id = window_id_t(-1) ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::state_object_t base_state ;
            motor::math::vec2ui_t size = motor::math::vec2ui_t(760,620) ;
            bool_t dirty = true, ready = false ;
            std::atomic_size_t executed {0} ;
            size_t active = 0 ;
            bool_t point[light_count] = {} ;
            vec3_t light_value[light_count] ;
            vec3_t light_color[light_count] ;
            float_t light_intensity[light_count] = {} ;
            window_data( void_t ) noexcept = default ;
            window_data( window_data const & ) = delete ;
            window_data( window_data && rhs ) noexcept : id(rhs.id), camera(std::move(rhs.camera)),
                base_state(std::move(rhs.base_state)), size(rhs.size), dirty(rhs.dirty), ready(rhs.ready),
                executed(rhs.executed.load()), active(rhs.active)
            {
                // Only moved during app construction, before queued lambdas borrow this.
                assert(id == window_id_t(-1)) ;
                for( size_t i = 0 ; i < light_count ; ++i )
                {
                    point[i] = rhs.point[i] ; light_value[i] = rhs.light_value[i] ;
                    light_color[i] = rhs.light_color[i] ; light_intensity[i] = rhs.light_intensity[i] ;
                }
            }
        } ;

        options _opts ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _cube ;
        motor::graphics::msl_object_mtr_t _shaders[2] = {} ;
        motor::graphics::state_object_t _light_state ;
        motor::scene::logic_group_mtr_t _root = nullptr ;
        motor::scene::light_collector_visitor_t _collector ;
        motor::vector<motor::gfx::light_mtr_t> _collected ; // borrowed for this frame
        motor::vector<object> _objects ;
        motor::gfx::directional_light_mtr_t _sun = nullptr ; // borrowed from light_component
        motor::gfx::point_light_mtr_t _points[2] = {} ;
        size_t _markers[2] = {} ;
        window_data _windows[2] ;
        bool_t _enabled[light_count] = {true,true,true} ;
        bool_t _paused = false, _reverse = false, _quitting = false, _scene_ready = false ;
        size_t _phase = 0 ;
        float_t _time = 0 ;
        clock_t::time_point _started ;

        size_t count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        bool_t all_ready( void_t ) const noexcept
        { return _windows[0].ready && (count() == 1 || _windows[1].ready) ; }

        void_t fail( char_cptr_t message ) noexcept
        {
            motor::log::global_t::test(message,false) ;
            _quitting = true ; close() ;
        }

        void_t make_geometry( void_t ) noexcept
        {
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element(motor::graphics::vertex_attribute::position,
                    motor::graphics::type::tfloat,motor::graphics::type_struct::vec3)
                .add_layout_element(motor::graphics::vertex_attribute::normal,
                    motor::graphics::type::tfloat,motor::graphics::type_struct::vec3) ;
            vb.resize(24).update<vertex>([]( vertex * v, size_t )
            {
                vec3_t const p[] = {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                    {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}} ;
                uint_t const faces[6][4] = {{4,5,6,7},{1,0,3,2},{0,4,7,3},{5,1,2,6},{3,7,6,2},{0,1,5,4}} ;
                vec3_t const normals[] = {{0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}} ;
                for( size_t f = 0 ; f < 6 ; ++f )
                    for( size_t c = 0 ; c < 4 ; ++c ) v[f*4+c] = {p[faces[f][c]],normals[f]} ;
            }) ;
            auto ib = motor::graphics::index_buffer_t().set_layout_element(motor::graphics::type::tuint) ;
            ib.resize(36).update<uint_t>([]( uint_t * ids, size_t )
            {
                uint_t const tri[] = {0,1,2,0,2,3} ;
                for( uint_t f = 0 ; f < 6 ; ++f )
                    for( size_t c = 0 ; c < 6 ; ++c ) ids[f*6+c] = f*4+tri[c] ;
            }) ;
            _cube = motor::graphics::geometry_object_t("component_cube",
                motor::graphics::primitive_type::triangles,std::move(vb),std::move(ib)) ;
        }

        void_t make_shaders( void_t ) noexcept
        {
            for( size_t pass = 0 ; pass < 2 ; ++pass )
            {
                motor::string_t const name = pass == 0 ? "component_base" : "component_light" ;
                motor::string_t source = "config " + name + R"(
                {
                    vertex_shader
                    {
                        in vec3_t pos : position ; in vec3_t nrm : normal ;
                        out vec4_t pos : position ;
                        out vec3_t world_pos : texcoord0 ; out vec3_t world_nrm : texcoord1 ;
                        mat4_t u_world : world ; mat4_t u_view : view ; mat4_t u_proj : projection ;
                        mat4_t u_normal ;
                        void main()
                        {
                            vec4_t p = u_world * vec4_t( in.pos, 1.0 ) ;
                            out.pos = u_proj * u_view * p ;
                            out.world_pos = p.xyz ;
                            out.world_nrm = ( u_normal * vec4_t( in.nrm, 0.0 ) ).xyz ;
                        }
                    }
                    pixel_shader
                    {
                        in vec3_t world_pos : texcoord0 ; in vec3_t world_nrm : texcoord1 ;
                        out vec4_t color : color ; vec3_t u_albedo ;
                )" ;
                if( pass == 0 )
                {
                    source += R"(
                        float_t u_base_strength ;
                        void main() { out.color = vec4_t( u_albedo * u_base_strength, 1.0 ) ; }
                    )" ;
                }
                else
                {
                    // Radius has no binding yet; both point lights use a six-unit falloff.
                    source += R"(
                        vec3_t u_direction : light_direction ; vec3_t u_position : light_position ;
                        vec3_t u_eye : camera_position ; bool_t is_point_light ;
                        vec3_t u_light_color : light_color ; float_t u_light_intensity : light_intensity ;
                        void main()
                        {
                            vec3_t l = -u_direction ; float_t energy = u_light_intensity ;
                            if( is_point_light )
                            {
                                l = u_position - in.world_pos ;
                                float_t dist = length( l ) ;
                                float_t falloff = max( 1.0 - dist / 6.0, 0.0 ) ;
                                energy = energy * falloff * falloff ;
                            }
                            l = l / max( length( l ), 0.0001 ) ;
                            vec3_t n = normalize( in.world_nrm ) ;
                            vec3_t v = normalize( u_eye - in.world_pos ) ;
                            float_t diffuse = max( dot( n, l ), 0.0 ) ;
                            float_t specular = 0.0 ;
                            if( diffuse > 0.0 )
                            { specular = pow( max( dot( n, normalize( l + v ) ), 0.0 ), 32.0 ) * 0.18 ; }
                            vec3_t lit = (u_albedo * diffuse + as_vec3( specular )) ' u_light_color ;
                            out.color = vec4_t( lit * energy, 0.0 ) ;
                        }
                    )" ;
                }
                source += "} }" ;
                auto shader = motor::graphics::msl_object_t(name,true) ;
                shader.add(motor::graphics::msl_api_type::msl_4_0,source) ;
                shader.link_geometry(_cube.name()) ;
                _shaders[pass] = motor::shared(std::move(shader)) ;
            }
        }

        size_t add_object( vec3_t const position, vec3_t const scale, vec3_t const rotation,
            vec3_t const color, bool_t const marker = false ) noexcept
        {
            object entry ;
            auto leaf = motor::shared(motor::scene::logic_leaf_t()) ;
            auto trafo = motor::shared(motor::scene::trafo3d_component_t()) ;
            trafo->borrow_trafo_is()->set_value(trafo_t(scale,rotation,position)) ;
            entry.trafo = trafo ; leaf->add_component(motor::move(trafo)) ;
            auto set = motor::shared(motor::scene::msl_set_component_t()) ;
            for( size_t pass = 0 ; pass < (marker ? 1u : 2u) ; ++pass )
            {
                auto vars = motor::shared(motor::graphics::variable_set_t()) ;
                vars->data_variable<mat4_t>("u_world")->set(mat4_t::make_identity()) ;
                vars->data_variable<mat4_t>("u_view")->set(mat4_t::make_identity()) ;
                vars->data_variable<mat4_t>("u_proj")->set(mat4_t::make_identity()) ;
                vars->data_variable<mat4_t>("u_normal")->set(trafo_t(
                    vec3_t(1.0f/scale.x(),1.0f/scale.y(),1.0f/scale.z()),rotation,vec3_t(0)).get_transformation()) ;
                vars->data_variable<vec3_t>("u_albedo")->set(color) ;
                if( pass == 0 ) vars->data_variable<float_t>("u_base_strength")->set(marker ? 0.95f : 0.07f) ;
                else
                {
                    vars->data_variable<vec3_t>("u_direction")->set(vec3_t(0,-1,0)) ;
                    vars->data_variable<vec3_t>("u_position")->set(vec3_t(0)) ;
                    vars->data_variable<vec3_t>("u_eye")->set(vec3_t(0,4,10)) ;
                    vars->data_variable<bool_t>("is_point_light")->set(false) ;
                    // Zero defaults expose a missing visitor update instead of hiding it.
                    vars->data_variable<vec3_t>("u_light_color")->set(vec3_t(0)) ;
                    vars->data_variable<float_t>("u_light_intensity")->set(0.0f) ;
                }
                size_t const id = _shaders[pass]->add_variable_set(motor::move(vars)) ;
                auto comp = motor::shared(motor::scene::msl_component_t(motor::share(_shaders[pass]),id,0)) ;
                if( pass == 0 ) entry.base = comp ; else entry.lit = comp ;
                set->add_component(pass,motor::move(comp)) ;
            }
            leaf->add_component(motor::move(set)) ;
            _root->add_child(motor::move(leaf)) ;
            _objects.emplace_back(entry) ;
            return _objects.size()-1 ;
        }

        void_t make_lights( void_t ) noexcept
        {
            // Group + leaf placement, including two lights in one component.
            auto group = motor::shared(motor::scene::logic_group_t()) ;
            auto lc = motor::shared(motor::scene::light_component_t()) ;
            auto sun = motor::shared(motor::gfx::directional_light_t(
                .32f,vec4_t(1.0f,.92f,.8f,1),vec3_t(-.4f,-1,-.3f).normalized())) ;
            _sun = sun ; lc->add_light(motor::move(sun)) ;
            auto first = motor::shared(motor::gfx::point_light_t(1.4f,vec4_t(1,.3f,.12f,1),vec3_t(-3,2,0),6)) ;
            _points[0] = first ; lc->add_light(motor::move(first)) ;
            group->add_component(motor::move(lc)) ;
            {
                auto leaf = motor::shared(motor::scene::logic_leaf_t()) ;
                auto second = motor::shared(motor::gfx::point_light_t(1.8f,vec4_t(.18f,.5f,1,1),vec3_t(3,2,0),6)) ;
                _points[1] = second ;
                leaf->add_component(motor::shared(motor::scene::light_component_t(motor::move(second)))) ;
                group->add_child(motor::move(leaf)) ;
            }
            _root->add_child(motor::move(group)) ;
        }

        void_t collect_lights( void_t ) noexcept
        {
            _collector.clear_lights() ; _collected.clear() ;
            motor::scene::node_t::traverser(_root).apply(&_collector) ;
            size_t components = 0 ;
            _collector.for_each_light([&]( size_t const, motor::scene::light_component_mtr_t comp )
            {
                ++components ;
                comp->for_each_light([&]( motor::gfx::light_mtr_t light ) { _collected.emplace_back(light) ; }) ;
            }) ;
            if( components != 2 || _collected.size() != light_count ||
                std::count(_collected.begin(),_collected.end(),_sun) != 1 ||
                std::count(_collected.begin(),_collected.end(),_points[0]) != 1 ||
                std::count(_collected.begin(),_collected.end(),_points[1]) != 1 )
            { fail("[light components] collector returned missing or duplicate lights") ; return ; }
            if( _reverse ) std::reverse(_collected.begin(),_collected.end()) ;
        }

        bool_t enabled( motor::gfx::light_mtr_t light ) const noexcept
        { return _enabled[light == _sun ? 0 : (light == _points[0] ? 1 : 2)] ; }

    public:
        light_app( options opts, bool_t * passed ) noexcept : _opts(opts), _passed(passed) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = clock_t::now() ;
            make_geometry() ; make_shaders() ;
            _root = motor::shared(motor::scene::logic_group_t()) ;
            add_object(vec3_t(0,-.125f,0),vec3_t(14,.25f,12),vec3_t(0),vec3_t(.42f,.46f,.48f)) ;
            vec3_t const colors[] = {{.85f,.22f,.12f},{.12f,.55f,.8f},{.72f,.65f,.18f},
                {.22f,.65f,.38f},{.6f,.23f,.52f},{.75f,.75f,.78f}} ;
            for( size_t i = 0 ; i < 6 ; ++i )
            {
                float_t const height = 1.2f + float_t(i%3)*.7f ;
                add_object(vec3_t(float_t(i%3)*3.6f-3.6f,height*.5f,float_t(i/3)*4-2),
                    vec3_t(1.5f,height,1.5f),vec3_t(0,float_t(i)*.22f,0),colors[i]) ;
            }
            make_lights() ;
            for( size_t i = 0 ; i < 2 ; ++i )
                _markers[i] = add_object(_points[i]->get_position(),vec3_t(.18f),vec3_t(0),_points[i]->color(),true) ;
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ;
                s.depth_s.ss.do_depth_write = false ; s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = true ;
                s.blend_s.ss.blend_func = motor::graphics::blend_function::add ;
                s.blend_s.ss.src_blend_factor = motor::graphics::blend_factor::one ;
                s.blend_s.ss.dst_blend_factor = motor::graphics::blend_factor::one ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = false ;
                _light_state = motor::graphics::state_object_t("component_light_state") ;
                _light_state.add_render_state_set(s) ;
            }
            for( size_t w = 0 ; w < count() ; ++w )
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = true ;
                s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ;
                s.clear_s.ss.do_color_clear = true ; s.clear_s.ss.do_depth_clear = true ;
                s.clear_s.ss.clear_color = vec4_t(.025f,.03f,.035f,1) ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
                s.view_s.ss.vp = motor::math::vec4ui_t(0,0,760,620) ;
                _windows[w].base_state = motor::graphics::state_object_t("component_base_state_"+motor::to_string(w)) ;
                _windows[w].base_state.add_render_state_set(s) ;
                motor::application::window_info_t info ;
                bool_t const d3d = _opts.d3d || w == 1 ;
                info.window_name = d3d ? "motor | light components | D3D11" : "motor | light components | GL4" ;
                info.x = int_t(30+w*780) ; info.y = 60 ; info.w = 760 ; info.h = 620 ;
                info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                _windows[w].id = create_window(info) ;
                send_window_message(_windows[w].id,[]( auto & window )
                {
                    window.send_message(motor::application::show_message({true})) ;
                    window.send_message(motor::application::vsync_message_t({true})) ;
                }) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            if( !_paused && !_opts.still ) _time += data.sec_dt ;
            _points[0]->set_position(vec3_t(-2.4f+2*std::sin(_time*.7f),2.5f,2.6f*std::cos(_time*.7f))) ;
            _points[1]->set_position(vec3_t(2.4f+1.7f*std::cos(_time*.55f),1.6f,-2.5f*std::sin(_time*.55f))) ;
            _sun->set_direction(vec3_t(-.4f-.2f*std::sin(_time*.2f),-1,-.3f).normalized()) ;
            for( size_t i = 0 ; i < 2 ; ++i )
                _objects[_markers[i]].trafo->borrow_trafo_is()->set_value(trafo_t(vec3_t(.18f),vec3_t(0),_points[i]->get_position())) ;
            collect_lights() ;
            for( size_t w = 0 ; w < count() ; ++w )
            {
                auto & window = _windows[w] ;
                window.camera.make_perspective_fov(float_t(window.size.x())/float_t(window.size.y()),1,.85f,.1f,80) ;
                window.camera.look_at(vec3_t(12,10,16),vec3_t(0,1,0),vec3_t(0,1,0)) ;
            }
            _scene_ready = all_ready() ;
            if( _scene_ready )
            {
                for( auto & obj : _objects )
                {
                    obj.base->render_update(&_windows[0].camera) ;
                    if( obj.lit ) obj.lit->render_update(&_windows[0].camera) ;
                }
            }
            motor::scene::variable_update_visitor_t update ;
            motor::scene::node_t::traverser(_root).apply(&update) ;
            motor::scene::trafo_visitor_t transform ;
            motor::scene::node_t::traverser(_root).apply(&transform) ;
        }

        void_t on_first_frame( window_id_t const, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            fe->configure<motor::graphics::geometry_object_t>(&_cube) ;
            fe->configure<motor::graphics::state_object_t>(&_light_state) ;
            for( auto * shader : _shaders ) fe->configure<motor::graphics::msl_object_t>(shader) ;
        }

        void_t on_render( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            if( _quitting ) return ;
            size_t const w = id == _windows[0].id ? 0 : 1 ;
            auto & window = _windows[w] ;
            if( window.dirty )
            {
                window.base_state.access_render_state(0,[&]( auto & s )
                { s.view_s.ss.vp = motor::math::vec4ui_t(0,0,window.size.x(),window.size.y()) ; return true ; }) ;
                fe->configure<motor::graphics::state_object_t>(&window.base_state) ; window.dirty = false ;
            }
            window.ready = true ;
            for( auto * shader : _shaders )
            {
                auto const status = fe->decode(shader) ;
                if( status.second == motor::graphics::result::failed ) { fail("[light components] shader failed") ; return ; }
                window.ready &= status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
            }
            if( !_scene_ready || !all_ready() ) return ;
            fe->push(&window.base_state) ;
            motor::scene::render_visitor_t base(0,w,fe,&window.camera) ;
            motor::scene::node_t::traverser(_root).apply(&base) ;
            fe->push(&_light_state) ;
            size_t active = 0 ;
            for( auto * light : _collected )
            {
                if( !enabled(light) ) continue ;
                size_t const rid = w*light_count+active ;
                // Resolve IDs before submitting; verification never updates a queued subset.
                for( auto & obj : _objects )
                    if( obj.lit ) obj.subsets[w][active] = obj.lit->render_update(rid,&window.camera) ;
                motor::scene::light_pass_render_visitor_t pass(1,rid,fe,&window.camera,light) ;
                motor::scene::node_t::traverser(_root).apply(&pass) ;
                window.point[active] = light->get_light_type() == motor::gfx::light_type::point ;
                window.light_color[active] = light->color() ;
                window.light_intensity[active] = light->intensity() ;
                window.light_value[active] = window.point[active] ?
                    static_cast<motor::gfx::point_light_ptr_t>(light)->get_position() :
                    static_cast<motor::gfx::directional_light_ptr_t>(light)->get_direction() ;
                ++active ;
            }
            window.active = active ;
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            fe->fence([&window]( void_t ) { window.executed.fetch_add(1) ; }) ;
        }

        bool_t verify( void_t ) noexcept
        {
            for( auto const & obj : _objects )
            {
                if( !obj.lit ) continue ;
                motor::vector<size_t> ids ;
                for( size_t w = 0 ; w < count() ; ++w )
                {
                    auto const & window = _windows[w] ;
                    for( size_t l = 0 ; l < window.active ; ++l )
                    {
                        size_t const id = obj.subsets[w][l] ;
                        if( std::find(ids.begin(),ids.end(),id) != ids.end() ) return false ;
                        ids.emplace_back(id) ;
                        auto * vars = _shaders[1]->borrow_variable_set(id).vs ;
                        if( !vars ) return false ;
                        auto * point = vars->find_data_variable<bool_t>("is_point_light") ;
                        auto * value = vars->find_data_variable<vec3_t>(window.point[l] ? "u_position" : "u_direction") ;
                        auto * world = vars->find_data_variable<mat4_t>("u_world") ;
                        auto * view = vars->find_data_variable<mat4_t>("u_view") ;
                        auto * color = vars->find_data_variable<vec3_t>("u_light_color") ;
                        auto * intensity = vars->find_data_variable<float_t>("u_light_intensity") ;
                        if( !point || !value || !world || !view || point->get() != window.point[l] ) return false ;
                        if( !color || !intensity || !std::isfinite(intensity->get()) ||
                            std::abs(intensity->get()-window.light_intensity[l]) > .0001f )
                        {
                            motor::log::global_t::test("[light components] light color/intensity binding",false) ;
                            return false ;
                        }
                        for( size_t k = 0 ; k < 3 ; ++k )
                        {
                            if( !std::isfinite(value->get()[k]) || std::abs(value->get()[k]-window.light_value[l][k]) > .0001f ) return false ;
                            if( !std::isfinite(color->get()[k]) || std::abs(color->get()[k]-window.light_color[l][k]) > .0001f )
                            {
                                motor::log::global_t::test("[light components] light color value",false) ;
                                return false ;
                            }
                        }
                        for( size_t k = 0 ; k < 16 ; ++k )
                            if( std::abs(world->get()[k]-obj.trafo->get_computed().get_transformation()[k]) > .0001f ||
                                std::abs(view->get()[k]-window.camera.get_view_matrix()[k]) > .0001f ) return false ;
                    }
                }
            }
            return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( !_opts.smoke || _quitting || !_scene_ready ) return ;
            for( size_t w = 0 ; w < count() ; ++w ) if( _windows[w].executed.load() < 30 ) return ;
            size_t const expected[] = {3,1,2,0,3} ;
            bool_t ok = verify() ;
            for( size_t w = 0 ; w < count() ; ++w ) ok &= _windows[w].active == expected[_phase] ;
            motor::log::global_t::test("[light components] phase "+motor::to_string(_phase)+
                " collector / type / light values / color / intensity / transforms / distinct subsets",ok) ;
            if( !ok ) { fail("[light components] frame validation failed") ; return ; }
            if( ++_phase == 5 ) { *_passed = true ; _quitting = true ; close() ; return ; }
            for( size_t w = 0 ; w < count() ; ++w ) _windows[w].executed.store(0) ;
            _enabled[0] = _phase == 1 || _phase == 4 ;
            _enabled[1] = _enabled[2] = _phase == 2 || _phase == 4 ;
            _reverse = _phase == 4 ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting ) return false ;
            if( ImGui::Begin("Scene lights",nullptr,ImGuiWindowFlags_AlwaysAutoResize) )
            {
                ImGui::BeginDisabled(_opts.smoke) ;
                ImGui::Checkbox("Directional",&_enabled[0]) ;
                ImGui::Checkbox("Point A",&_enabled[1]) ; ImGui::Checkbox("Point B",&_enabled[2]) ;
                ImGui::Checkbox("Pause",&_paused) ; ImGui::Checkbox("Reverse order",&_reverse) ;
                ImGui::EndDisabled() ;
                ImGui::Text("Collected: %llu",static_cast<unsigned long long>(_collected.size())) ;
            }
            ImGui::End() ; return true ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( _opts.smoke && !_quitting && clock_t::now()-_started > std::chrono::seconds(60) )
                fail("[light components] smoke timeout") ;
        }

        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t event ) noexcept override
        {
            if( event.resize_changed && event.resize_msg.resize && event.resize_msg.w > 0 && event.resize_msg.h > 0 )
            {
                auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
                window.size = motor::math::vec2ui_t(uint_t(event.resize_msg.w),uint_t(event.resize_msg.h)) ; window.dirty = true ;
            }
            if( event.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_last_frame( window_id_t const id, frontend_t fe, motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto * shader : _shaders ) fe->release<motor::graphics::msl_object_t>(shader) ;
            fe->release<motor::graphics::geometry_object_t>(&_cube) ;
            fe->release<motor::graphics::state_object_t>(&_light_state) ;
            fe->release<motor::graphics::state_object_t>(&_windows[id == _windows[0].id ? 0 : 1].base_state) ;
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            _collector.clear_lights() ; _collected.clear() ; _objects.clear() ;
            _sun = nullptr ; _points[0] = _points[1] = nullptr ;
            motor::release(motor::move(_root)) ;
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
        if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else if( arg == "--dual" ) { opts.dual = true ; opts.d3d = false ; }
        else
        {
            motor::log::global_t::status("Usage: 14_light_components [--gl-only|--d3d-only|--dual] [--smoke] [--still]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
#ifndef _WIN32
    if( opts.dual || opts.d3d ) return 2 ;
#endif
    bool passed = false ;
    auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::light_app(opts,&passed))) ;
    int const code = carrier->exec() ; motor::release(motor::move(carrier)) ;
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    return code != 0 ? code : motor::memory::global_t::dump_to_std() != 0 || (opts.smoke && !passed) ? 1 : 0 ;
}
