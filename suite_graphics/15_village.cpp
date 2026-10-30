#include "capture_options.hpp"

#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/streamout_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/graphics/object/framebuffer_object.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/gfx/postprocess/hdr_postprocess_pipeline.h>
#include <motor/scene/node/logic_group.h>
#include <motor/scene/node/logic_leaf.h>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/light_component.hpp>
#include <motor/scene/component/graphics/msl_set_component.hpp>
#include <motor/scene/visitor/light_collector_visitor.hpp>
#include <motor/scene/visitor/variable_update_visitor.hpp>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/scene/visitor/graphics/light_pass_render_visitor.h>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include "village/geometry.hpp"
#include "village/dragon.hpp"
#include "village/shaders.hpp"
#include <chrono>
#include <cstdlib>
#include <type_traits>
#include <fstream>
#include <iomanip>
#include <locale>
#include "diagnostics/process_cpu_meter.hpp"
#include "diagnostics/sleep_probe.hpp"

namespace village
{
    constexpr size_t tile_count = 4 ;
    constexpr size_t model_count = 10 ; // houses, ground, props, plants, dragon, well, statue

    vec3 tile_offset( size_t const tile ) noexcept
    {
        return vec3((float_t(tile%2)-.5f)*33.0f,0,(float_t(tile/2)-.5f)*33.0f) ;
    }

    float_t tile_angle( size_t const tile ) noexcept { return float_t(tile)*pi*0.5f ; }

    motor::math::m3d::trafof_t tile_transform( size_t const tile ) noexcept
    {
        return motor::math::m3d::trafof_t(vec3(1),vec3(0,tile_angle(tile),0),tile_offset(tile)) ;
    }

    vec3 tile_point( size_t const tile, vec3 const p ) noexcept
    {
        float_t const a = tile_angle(tile) ;
        return tile_offset(tile)+vec3(std::cos(a)*p.x()+std::sin(a)*p.z(),p.y(),
            -std::sin(a)*p.x()+std::cos(a)*p.z()) ;
    }

    template< typename T > using output = motor::wire::output_slot<T> ;

    struct scene_controls : motor::scene::icomponent
    {
        output<vec4> * clock = nullptr, * dragon_clock = nullptr, * fire[tile_count] = {} ;
        output<vec3> * direction[2] = {}, * energy[3] = {} ;
        output<vec3> * shadow_settings = nullptr ;

        scene_controls( void_t ) noexcept
        {
            clock = create_output_slot<vec4>(vec4(0)) ;
            dragon_clock = create_output_slot<vec4>(vec4(0)) ;
            for( auto & slot : fire ) slot = create_output_slot<vec4>(vec4(0)) ;
            shadow_settings = create_output_slot<vec3>(vec3(1,0.00035f,0.0008f)) ;
            for( auto & slot : direction ) slot = create_output_slot<vec3>(vec3(0)) ;
            for( auto & slot : energy ) slot = create_output_slot<vec3>(vec3(0)) ;
        }
    } ;

    struct options
    {
        sample_support::capture_options capture ;
        bool_t dual = false, d3d = false, smoke = false, still = false ;
        bool_t benchmark = false, vsync = true ;
        bool_t sleep_probe = false ;
        size_t cpu_sleep_us = 0 ;
        int detail = 1 ;
        size_t particles = 8192, tiles = 4, total_particles = 0 ;
        size_t warmup = 180, frames = 600 ;
    } ;

    class village_app : public motor::application::app
    {
        struct particle_seed { vec4 position, color, tile ; } ;
        struct light_controls
        {
            float_t color[3] = {1,1,1} ;
            float_t intensity = 1 ;

            void_t reset( motor::gfx::light_mtr_t light ) noexcept
            {
                for( size_t i = 0 ; i < 3 ; ++i ) color[i] = light->color()[i] ;
                intensity = light->intensity() ;
            }

            vec3 energy( void_t ) const noexcept
            {
                return vec3(color[0],color[1],color[2])*intensity ;
            }
        } ;
        struct draw_counts
        {
            uint_t scene = 0, shadow = 0, capture = 0, particles = 0, post = 0 ;
            uint_t total( void_t ) const noexcept { return scene+shadow+capture+particles+post ; }
        } ;
        struct window_data
        {
            window_id_t id = window_id_t(-1) ;
            motor::gfx::generic_camera_t camera ;
            motor::graphics::state_object_t present ;
            motor::math::vec2ui_t dimensions = motor::math::vec2ui_t(1280,800) ;
            bool_t viewport_dirty = true, ready = false ;
            size_t frames = 0, post_frames = 0, particle_frames = 0 ;
            size_t dragon_frames = 0, hidden_dragon_frames = 0, fire_frames = 0 ;
            size_t shadow_frames = 0, unshadowed_frames = 0 ;
            draw_counts draws ;
            std::chrono::steady_clock::time_point benchmark_stamp, previous_stamp ;
            motor::vector<double> benchmark_intervals ;
            motor::graphics::data_variable<motor::math::mat4f_t> * matrix = nullptr ;
            motor::graphics::data_variable<vec3> * right = nullptr, * up = nullptr ;
            motor::graphics::data_variable<vec4> * clock = nullptr, * dragon = nullptr ;
        } ;

        struct scene_object
        {
            size_t model = 0, tile = 0, geometry = 0, triangles = 0 ;
            material_id material = material_id::wood ;
            // Borrowed from the graph; the graph owns components and nodes.
            motor::scene::msl_component_mtr_t msl = nullptr ;
            motor::scene::msl_component_mtr_t shadow_msl = nullptr ;
            motor::scene::trafo3d_component_mtr_t trafo = nullptr ;
            size_t subset[2] = {size_t(-1),size_t(-1)} ;
            size_t shadow_subset[2] = {size_t(-1),size_t(-1)} ;
        } ;

        options _opts ;
        bool_t * _passed ;
        window_data _windows[2] ;
        motor::vector<motor::graphics::geometry_object_t> _geometry ;
        motor::graphics::geometry_object_t _seeds ;
        motor::graphics::streamout_object_t _stream ;
        motor::graphics::msl_object_mtr_t _surface = nullptr, _capture = nullptr, _particles = nullptr ;
        motor::graphics::msl_object_mtr_t _shadow = nullptr ;
        motor::graphics::framebuffer_object_t _shadow_fb ;
        motor::graphics::state_object_t _shadow_state ;
        motor::gfx::generic_camera_t _moon_camera ;
        motor::gfx::single_shadow_data_t _moon_shadow = {motor::math::mat4f_t::make_identity(),
            motor::math::mat4f_t::make_identity(),"village_moon.depth"} ;
        motor::graphics::state_object_t _opaque_state, _hdr_state, _particle_state ;
        motor::gfx::hdr_postprocess_pipeline_mtr_t _post = nullptr ;
        motor::scene::logic_group_mtr_t _root = nullptr ;
        motor::scene::logic_group_mtr_t _village = nullptr ; // borrowed
        motor::scene::logic_group_mtr_t _dragon_node = nullptr ; // borrowed
        motor::scene::trafo3d_component_mtr_t _model_trafos[tile_count][model_count] = {} ; // borrowed
        scene_controls * _controls = nullptr ; // borrowed
        motor::vector<scene_object> _objects ;
        uint_t _village_draws = 0, _dragon_draws = 0 ;
        size_t _material_draws[size_t(material_id::count)] = {} ;
        motor::scene::light_collector_visitor_t _light_collector ;
        motor::gfx::directional_light_mtr_t _directional[2] = {} ; // borrowed
        motor::gfx::point_light_mtr_t _fire_light[tile_count] = {} ; // borrowed
        light_controls _light_controls[3] ;
        size_t _collected_lights = 0 ;
        bool_t _scene_ready = false ;
        size_t _triangles = 0, _vertices = 0 ;
        size_t _smoke_phase = 0 ;
        float_t _time = 0, _yaw = 24, _height = 48, _distance = 86, _emission = 1.0f ;
        bool_t _orbit = false, _paused = false, _post_enabled = true, _particles_enabled = true ;
        bool_t _dragon_enabled = true, _breath = true, _frame_dragon = true ;
        bool_t _frame_fire = true ;
        bool_t _shadows = true, _frame_shadows = true ;
        float_t _shadow_bias = 0.00035f ;
        float_t _moon_yaw = 0, _moon_elevation = 45, _moon_speed = 3 ;
        bool_t _moon_orbit = true ;
        motor::math::mat4f_t _smoke_moon_view = motor::math::mat4f_t::make_identity() ;
        bool_t _frame_post = true, _frame_particles = true, _quitting = false ;
        std::chrono::steady_clock::time_point _started ;
        struct timing_sample { double frame, graphics, prepare ; } ;
        motor::vector<timing_sample> _timings ;
        std::chrono::steady_clock::time_point _previous_frame ;
        std::chrono::steady_clock::time_point _measurement_begin ;
        village_diagnostics::process_cpu_meter _cpu_meter ;
        size_t _warm_frames = 0 ;
        size_t _benchmark_rendered = 0 ;
        double _graphics_ms = 0, _prepare_ms = 0 ;

        static double elapsed_ms( std::chrono::steady_clock::time_point const start ) noexcept
        {
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count() ;
        }

        void_t benchmark_frame( void_t ) noexcept
        {
            if( _benchmark_rendered != window_count() )
            {
                _warm_frames = 0 ; _timings.clear() ;
                for( auto & window : _windows ) window.benchmark_intervals.clear() ;
                return ;
            }
            auto const now = std::chrono::steady_clock::now() ;
            double const frame = std::chrono::duration<double,std::milli>(now-_previous_frame).count() ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto & window = _windows[w] ;
                if( _warm_frames >= _opts.warmup ) window.benchmark_intervals.emplace_back(
                    std::chrono::duration<double,std::milli>(window.benchmark_stamp-window.previous_stamp).count()) ;
                window.previous_stamp = window.benchmark_stamp ;
            }
            if( _warm_frames == _opts.warmup ) _measurement_begin = _previous_frame ;
            _previous_frame = now ;
            if( _warm_frames++ < _opts.warmup )
            {
                if( _warm_frames == _opts.warmup ) _cpu_meter.start() ;
                return ;
            }
            _timings.push_back({frame,_graphics_ms,_prepare_ms}) ;
            if( _timings.size() != _opts.frames ) return ;
            _cpu_meter.finish() ;
            motor::string_t frame_csv ;
#if defined(_MSC_VER)
            char * path = nullptr ;
            size_t path_size = 0 ;
            if( _dupenv_s(&path, &path_size, "MOTOR_VILLAGE_FRAME_CSV") == 0 && path )
                frame_csv = path ;
            std::free(path) ;
#else
            if( auto const path = std::getenv("MOTOR_VILLAGE_FRAME_CSV") ) frame_csv = path ;
#endif
            if( !frame_csv.empty() )
            {
                std::ofstream out(frame_csv.c_str()) ;
                out.imbue(std::locale::classic()) ;
                out << "frame,frame_ms,graphics_ms,prepare_ms,window0_ms" ;
                if( _opts.dual ) out << ",window1_ms" ;
                out << '\n' << std::fixed << std::setprecision(6) ;
                for( size_t i = 0 ; i < _timings.size() ; ++i )
                {
                    auto const & t = _timings[i] ;
                    out << i << ',' << t.frame << ',' << t.graphics << ',' << t.prepare ;
                    for( size_t w = 0 ; w < window_count() ; ++w ) out << ',' << _windows[w].benchmark_intervals[i] ;
                    out << '\n' ;
                }
                out.flush() ;
                if( !out.good() ) { fail("[village benchmark] frame CSV write") ; return ; }
            }
            if( !verify_scene() ) { fail("[village benchmark] transforms, subsets and lights") ; return ; }
            uint_t total_draws = 0 ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto const & draws = _windows[w].draws ;
                if( draws.scene != _village_draws+_dragon_draws || draws.shadow != draws.scene ||
                    draws.capture != 1 || draws.particles != 1 || draws.post != 13 ||
                    _windows[w].benchmark_intervals.size() != _opts.frames )
                { fail("[village benchmark] expected per-window draws and frame counts") ; return ; }
                total_draws += draws.total() ;
            }
            double mean = 0, graphics = 0, prepare = 0 ;
            motor::vector<double> times ; times.reserve(_timings.size()) ;
            for( auto const & sample : _timings )
            {
                mean += sample.frame ; graphics += sample.graphics ; prepare += sample.prepare ;
                times.push_back(sample.frame) ;
            }
            double const count = double(_timings.size()) ;
            mean /= count ; graphics /= count ; prepare /= count ;
            std::sort(times.begin(),times.end()) ;
            double const median = times.size()%2 ? times[times.size()/2] :
                (times[times.size()/2-1]+times[times.size()/2])*0.5 ;
            double const p95 = times[size_t(std::ceil(0.95*count))-1] ;
            motor::log::global_t::status<1024>(
                "VILLAGE_BENCH backend=%s tiles=%zu detail=%s vsync=0 frames=%zu warmup=%zu width=%u height=%u triangles=%zu particles=%zu draws=%u mean_ms=%.4f median_ms=%.4f p95_ms=%.4f fps=%.2f graphics_ms=%.4f prepare_ms=%.4f",
                _opts.dual ? "dual" : _opts.d3d ? "d3d11" : "gl4",_opts.tiles,_opts.detail == 0 ? "low" : _opts.detail == 1 ? "high" : "ultra",
                _opts.frames,_opts.warmup,_windows[0].dimensions.x(),_windows[0].dimensions.y(),
                _triangles,particle_count(),total_draws,mean,median,p95,1000.0/mean,graphics,prepare) ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto intervals = _windows[w].benchmark_intervals ;
                double sum = 0 ; for( auto const value : intervals ) sum += value ;
                std::sort(intervals.begin(),intervals.end()) ;
                motor::log::global_t::status<512>(
                    "VILLAGE_VIEW window=%zu backend=%s frames=%zu draws=%u mean_ms=%.4f p95_ms=%.4f p99_ms=%.4f max_ms=%.4f",
                    w,_opts.d3d || w == 1 ? "d3d11" : "gl4",intervals.size(),_windows[w].draws.total(),
                    sum/double(intervals.size()),intervals[size_t(std::ceil(.95*double(intervals.size())))-1],
                    intervals[size_t(std::ceil(.99*double(intervals.size())))-1],intervals.back()) ;
            }
            motor::log::global_t::status<256>("VILLAGE_BENCH_WINDOW start_ns=%lld end_ns=%lld",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(_measurement_begin.time_since_epoch()).count()),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count())) ;
            if( _opts.sleep_probe && !village_diagnostics::sleep_probe(frame_csv) )
            { fail("[village benchmark] sleep probe CSV write") ; return ; }
            *_passed = true ; _quitting = true ; close() ;
        }

        size_t window_count( void_t ) const noexcept { return _opts.dual ? 2 : 1 ; }
        size_t particle_count( void_t ) const noexcept
        { return _opts.total_particles ? _opts.total_particles : _opts.tiles*_opts.particles ; }

        static bool_t valid_mesh( mesh const & m ) noexcept
        {
            if( m.vertices.empty() || m.indices.empty() || m.indices.size()%3 != 0 ) return false ;
            if( m.triangle_materials.size() != m.indices.size()/3 ) return false ;
            for( auto const id : m.triangle_materials ) if( size_t(id) >= size_t(material_id::count) ) return false ;
            for( auto const index : m.indices ) if( index >= m.vertices.size() ) return false ;
            for( auto const & v : m.vertices )
            {
                auto const p = v.position ; auto const n = v.normal ;
                if( !std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z()) ||
                    !std::isfinite(n.dot(n)) || std::abs(n.dot(n)-1.0f) > 0.01f ) return false ;
            }
            return true ;
        }

        void_t fail( motor::string_cref_t message ) noexcept
        {
            if( !_quitting ) motor::log::global_t::test(message,false) ;
            _quitting = true ; close() ;
        }

        template< typename T >
        void_t connect_shader( motor::scene::msl_component_mtr_t comp,
            motor::graphics::variable_set_mtr_t vars, motor::string_cref_t name, output<T> * slot ) noexcept
        {
            vars->data_variable<T>(name.c_str())->set(slot->get_value()) ;
            auto & inputs = comp->borrow_shader_inputs() ;
            inputs.borrow_or_add(name,motor::shared(motor::wire::input_slot<T>(slot->get_value()))) ;
            if( !inputs.connect(name,motor::share(slot)) ) fail("[village] shader-slot connection") ;
        }

        void_t make_scene( void_t ) noexcept
        {
            // Duplicate scene instances only; geometry links remain shared across tiles.
            auto const prototypes = std::move(_objects) ;
            _objects.clear() ; _triangles = 0 ; _village_draws = 0 ; _dragon_draws = 0 ;
            for( auto & draws : _material_draws ) draws = 0 ;
            for( size_t tile = 0 ; tile < _opts.tiles ; ++tile )
            {
                for( auto const & prototype : prototypes )
                {
                    if( (prototype.model == 8 && tile%2 == 1) || (prototype.model == 9 && tile%2 == 0) ) continue ;
                    auto object = prototype ; object.tile = tile ;
                    _objects.emplace_back(object) ;
                    _triangles += object.triangles ;
                    ++_material_draws[size_t(object.material)] ;
                    if( object.model == 7 ) ++_dragon_draws ; else ++_village_draws ;
                }
            }
            _root = motor::shared(motor::scene::logic_group_t()) ;
            auto controls = motor::shared(scene_controls()) ;
            _controls = controls ; _root->add_component(motor::move(controls)) ;
            auto group = motor::shared(motor::scene::logic_group_t()) ;
            _village = group ; _root->add_child(motor::move(group)) ;
            auto dragons = motor::shared(motor::scene::logic_group_t()) ;
            _dragon_node = dragons ; _root->add_child(motor::move(dragons)) ;
            motor::scene::logic_group_mtr_t models[tile_count][model_count] = {} ; // borrowed
            for( size_t tile = 0 ; tile < _opts.tiles ; ++tile )
            {
                auto village_tile = motor::shared(motor::scene::logic_group_t()) ;
                auto dragon_tile = motor::shared(motor::scene::logic_group_t()) ;
                for( auto * node : {village_tile.mtr(),dragon_tile.mtr()} )
                {
                    auto trafo = motor::shared(motor::scene::trafo3d_component_t()) ;
                    trafo->borrow_trafo_is()->set_value(tile_transform(tile)) ;
                    node->add_component(motor::move(trafo)) ;
                }
                for( size_t i = 0 ; i < model_count ; ++i )
                {
                    auto model = motor::shared(motor::scene::logic_group_t()) ;
                    models[tile][i] = model ;
                    auto trafo = motor::shared(motor::scene::trafo3d_component_t()) ;
                    _model_trafos[tile][i] = trafo ; model->add_component(motor::move(trafo)) ;
                    if( i == 7 ) dragon_tile->add_child(motor::move(model)) ;
                    else village_tile->add_child(motor::move(model)) ;
                }
                _village->add_child(motor::move(village_tile)) ;
                _dragon_node->add_child(motor::move(dragon_tile)) ;
            }
            for( size_t i = 0 ; i < _objects.size() ; ++i )
            {
                auto const model = _objects[i].model ;
                auto leaf = motor::shared(motor::scene::logic_leaf_t()) ;
                auto trafo = motor::shared(motor::scene::trafo3d_component_t()) ;
                _objects[i].trafo = trafo ; leaf->add_component(motor::move(trafo)) ;
                auto vars = motor::shared(motor::graphics::variable_set_t()) ;
                for( auto const name : {"u_world","u_view","u_proj","u_light_view","u_light_proj"} )
                    vars->data_variable<motor::math::mat4f_t>(name)->set(motor::math::mat4f_t::make_identity()) ;
                vars->data_variable<vec3>("u_eye")->set(vec3(0)) ;
                auto const base = _surface->add_variable_set(motor::share(vars)) ;
                auto comp = motor::shared(motor::scene::msl_component_t(motor::share(_surface),base,_objects[i].geometry)) ;
                _objects[i].msl = comp ;
                auto const response = materials[size_t(_objects[i].material)].response ;
                vars->data_variable<vec3>("u_material")->set(response) ;
                comp->borrow_shader_inputs().borrow_or_add("u_material",motor::shared(motor::wire::input_slot<vec3>(response))) ;
                connect_shader(comp,vars,"u_clock",model == 7 ? _controls->dragon_clock : _controls->clock) ;
                for( size_t l = 0 ; l < 2 ; ++l )
                    connect_shader(comp,vars,"u_direction_"+motor::to_string(l),_controls->direction[l]) ;
                for( size_t l = 0 ; l < 3 ; ++l )
                    connect_shader(comp,vars,"u_energy_"+motor::to_string(l),_controls->energy[l]) ;
                connect_shader(comp,vars,"u_fire",_controls->fire[_objects[i].tile]) ;
                connect_shader(comp,vars,"u_shadow_settings",_controls->shadow_settings) ;
                vars->texture_variable("u_shadow_map")->set(_moon_shadow.shadow_map) ;
                motor::release(motor::move(vars)) ;
                auto set = motor::shared(motor::scene::msl_set_component_t(0,motor::move(comp))) ;
                {
                    auto shadow_vars = motor::shared(motor::graphics::variable_set_t()) ;
                    for( auto const name : {"u_world","u_view","u_proj","u_light_view","u_light_proj"} )
                        shadow_vars->data_variable<motor::math::mat4f_t>(name)->set(motor::math::mat4f_t::make_identity()) ;
                    auto const shadow_base = _shadow->add_variable_set(motor::share(shadow_vars)) ;
                    auto shadow_comp = motor::shared(motor::scene::msl_component_t(motor::share(_shadow),shadow_base,_objects[i].geometry)) ;
                    _objects[i].shadow_msl = shadow_comp ;
                    connect_shader(shadow_comp,shadow_vars,"u_clock",model == 7 ? _controls->dragon_clock : _controls->clock) ;
                    motor::release(motor::move(shadow_vars)) ;
                    set->add_component(1,motor::move(shadow_comp)) ;
                }
                leaf->add_component(motor::move(set)) ;
                models[_objects[i].tile][model]->add_child(motor::move(leaf)) ;
            }
            {
                auto lights = motor::shared(motor::scene::logic_group_t()) ;
                auto lc = motor::shared(motor::scene::light_component_t()) ;
                vec3 const directions[] = {vec3(0.5f,-0.8f,-0.6f),vec3(-0.7f,-0.5f,0.4f)} ;
                vec3 const colors[] = {vec3(0.85f,1.03f,1.45f),vec3(0.10f,0.18f,0.26f)} ;
                for( size_t l = 0 ; l < 2 ; ++l )
                {
                    auto light = motor::shared(motor::gfx::directional_light_t(1.0f,colors[l],directions[l].normalized())) ;
                    _directional[l] = light ; lc->add_light(motor::move(light)) ;
                }
                for( size_t l = 0 ; l < 2 ; ++l ) _light_controls[l].reset(_directional[l]) ;
                lights->add_component(motor::move(lc)) ; _root->add_child(motor::move(lights)) ;
                for( size_t tile = 0 ; tile < _opts.tiles ; ++tile )
                {
                    auto node = motor::shared(motor::scene::logic_leaf_t()) ;
                    auto trafo = motor::shared(motor::scene::trafo3d_component_t()) ;
                    trafo->borrow_trafo_is()->set_value(tile_transform(tile)) ;
                    node->add_component(motor::move(trafo)) ;
                    auto fire = motor::shared(motor::gfx::point_light_t(3.0f,vec3(2.8f,0.72f,0.13f),vec3(2.8f,1.2f,2.6f),10.0f)) ;
                    _fire_light[tile] = fire ;
                    node->add_component(motor::shared(motor::scene::light_component_t(motor::move(fire)))) ;
                    _root->add_child(motor::move(node)) ;
                }
                _light_controls[2].reset(_fire_light[0]) ;
            }
            // motor uses one GL-style projection for both APIs. Keep the whole scene in
            // positive clip Z as well, so D3D's near clipping does not remove casters.
            _moon_camera.make_orthographic(128,128,0.1f,320.0f) ;
            auto const direction = _directional[0]->get_direction() ;
            _moon_yaw = std::atan2(-direction.x(),-direction.z())*180.0f/pi ;
            _moon_elevation = std::asin(-direction.y())*180.0f/pi ;
        }

        void_t update_moon( void_t ) noexcept
        {
            float_t const yaw = _moon_yaw*pi/180.0f, elevation = _moon_elevation*pi/180.0f ;
            vec3 const to_moon(std::sin(yaw)*std::cos(elevation),std::sin(elevation),
                std::cos(yaw)*std::cos(elevation)) ;
            _directional[0]->set_direction(to_moon*-1.0f) ;
        }

        void_t update_moon_shadow( void_t ) noexcept
        {
            auto const to_moon = _directional[0]->get_transformed_direction().normalized()*-1.0f ;
            vec3 const center(0,4,0) ;
            _moon_camera.look_at(center+to_moon*240.0f,vec3(0,1,0),center) ;
            _moon_shadow.proj = _moon_camera.get_proj_matrix() ;
            _moon_shadow.view = _moon_camera.get_view_matrix() ;
        }

        void_t update_scene( void_t ) noexcept
        {
            update_moon() ;
            _scene_ready = true ;
            for( size_t w = 0 ; w < window_count() ; ++w ) _scene_ready &= _windows[w].ready ;
            if( _scene_ready ) for( auto & object : _objects )
            {
                object.msl->render_update(&_windows[0].camera) ;
                object.shadow_msl->render_update(&_moon_camera) ;
            }
            _controls->shadow_settings->set_and_exchange(vec3(_frame_shadows ? 1.0f : 0.0f,_shadow_bias,0.0008f)) ;
            _controls->clock->set_and_exchange(vec4(_time,_emission,0,0)) ;
            _controls->dragon_clock->set_and_exchange(vec4(_time,_emission,1,0)) ;
            {
                motor::scene::variable_update_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply(&visitor) ;
            }
            {
                motor::scene::trafo_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply(&visitor) ;
            }
            // World-space light values and the shadow camera follow the transform pass.
            update_moon_shadow() ;
            _light_collector.clear_lights() ; _collected_lights = 0 ;
            motor::scene::node_t::traverser(_root).apply(&_light_collector) ;
            _light_collector.for_each_light([&]( size_t const, motor::scene::light_component_mtr_t comp )
            {
                comp->for_each_light([&]( motor::gfx::light_mtr_t light )
                {
                    ++_collected_lights ;
                    for( size_t l = 0 ; l < 2 ; ++l ) if( light == _directional[l] )
                    {
                        _controls->direction[l]->set_and_exchange(_directional[l]->get_transformed_direction()) ;
                        _controls->energy[l]->set_and_exchange(_light_controls[l].energy()) ;
                    }
                    for( size_t tile = 0 ; tile < _opts.tiles ; ++tile ) if( light == _fire_light[tile] )
                    {
                        _controls->fire[tile]->set_and_exchange(vec4(_fire_light[tile]->get_transformed_position(),_fire_light[tile]->get_radius())) ;
                        _controls->energy[2]->set_and_exchange(_light_controls[2].energy()) ;
                    }
                }) ;
            }) ;
            // Only surface shader inputs received new values after the graph-wide sync.
            for( auto & object : _objects )
            {
                motor::scene::icomponent::visitor_sync_accessor::sync_inputs(object.msl) ;
            }
        }

        template< typename T >
        static bool_t matches( motor::graphics::variable_set_mtr_t set, char_cptr_t name, T const & expected ) noexcept
        {
            motor::graphics::data_variable<T> * var = nullptr ;
            set->for_each_data_variable([&]( motor::string_in_t key, motor::graphics::ivariable_ptr_t value )
            { if( key == name ) var = dynamic_cast<motor::graphics::data_variable<T> *>(value) ; }) ;
            if( !var ) { motor::log::global_t::error(motor::string_t("[village] missing variable: ")+name) ; return false ; }
            constexpr size_t n = std::is_same_v<T,motor::math::mat4f_t> ? 16 : (std::is_same_v<T,vec4> ? 4 : 3) ;
            for( size_t i = 0 ; i < n ; ++i )
                if( !std::isfinite(var->get()[i]) || std::abs(var->get()[i]-expected[i]) > 0.0001f )
                { motor::log::global_t::error(motor::string_t("[village] variable mismatch: ")+name+" at "+motor::to_string(i)) ; return false ; }
            return true ;
        }

        bool_t verify_scene( void_t ) noexcept
        {
            if( _collected_lights != 2+_opts.tiles || _objects.size() != _village_draws+_dragon_draws ) return false ;
            for( size_t tile = 0 ; tile < _opts.tiles ; ++tile )
            {
                auto const expected = tile_point(tile,vec3(2.8f,1.2f,2.6f)) ;
                if( (_fire_light[tile]->get_transformed_position()-expected).length() > 0.0001f ) return false ;
                bool_t has_well = false, has_statue = false ;
                for( auto const & object : _objects ) if( object.tile == tile )
                {
                    has_well |= object.model == 8 ; has_statue |= object.model == 9 ;
                }
                if( has_well != (tile%2 == 0) || has_statue != (tile%2 == 1) ) return false ;
            }
            for( size_t geo = 0 ; geo < _geometry.size() ; ++geo )
            {
                size_t uses = 0, model = model_count ;
                for( auto const & object : _objects ) if( object.geometry == geo ) { ++uses ; model = object.model ; }
                size_t const expected_uses = model == 8 ? (_opts.tiles+1)/2 : model >= 9 ? _opts.tiles/2 : _opts.tiles ;
                if( uses != expected_uses ) return false ;
            }
            size_t active = 0 ;
            for( auto const & entry : _surface->borrow_variable_sets() ) if( entry.vs ) ++active ;
            if( active != _objects.size()*(1+window_count()) ) return false ;
            active = 0 ;
            for( auto const & entry : _shadow->borrow_variable_sets() ) if( entry.vs ) ++active ;
            if( active != _objects.size()*(1+window_count()) ) return false ;
            for( size_t i = 0 ; i < _objects.size() ; ++i )
            {
                auto const & object = _objects[i] ;
                if( object.geometry >= _geometry.size() || object.tile >= _opts.tiles ) return false ;
                auto const expected_world = tile_transform(object.tile)*
                    _model_trafos[object.tile][object.model]->get_trafo() ;
                if( window_count() == 2 && object.subset[0] == object.subset[1] ) return false ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    if( object.subset[w] == size_t(-1) ) return false ;
                    auto * vars = _surface->borrow_variable_set(object.subset[w]).vs ;
                    if( !vars || !matches(vars,"u_world",expected_world.get_transformation()) ||
                        !matches(vars,"u_view",_windows[w].camera.get_view_matrix()) ||
                        !matches(vars,"u_proj",_windows[w].camera.get_proj_matrix()) ||
                        !matches(vars,"u_eye",_windows[w].camera.get_position()) ||
                        !matches(vars,"u_clock",(object.model == 7 ? _controls->dragon_clock : _controls->clock)->get_value()) ||
                        !matches(vars,"u_material",materials[size_t(object.material)].response) ||
                        !matches(vars,"u_shadow_settings",_controls->shadow_settings->get_value()) ||
                        !matches(vars,"u_fire",_controls->fire[object.tile]->get_value()) ) return false ;
                    // Hidden leaves are deliberately not visited by the light visitor.
                    if( (_frame_dragon || object.model != 7) &&
                        (!matches(vars,"u_light_view",_moon_shadow.view) || !matches(vars,"u_light_proj",_moon_shadow.proj)) ) return false ;
                    if( object.shadow_subset[w] == size_t(-1) ) return false ;
                    auto * shadow_vars = _shadow->borrow_variable_set(object.shadow_subset[w]).vs ;
                    if( !shadow_vars || !matches(shadow_vars,"u_world",object.trafo->get_computed().get_transformation()) ||
                        !matches(shadow_vars,"u_view",_moon_shadow.view) || !matches(shadow_vars,"u_proj",_moon_shadow.proj) ||
                        !matches(shadow_vars,"u_clock",(object.model == 7 ? _controls->dragon_clock : _controls->clock)->get_value()) ) return false ;
                    for( size_t l = 0 ; l < 3 ; ++l )
                        if( !matches(vars,("u_energy_"+motor::to_string(l)).c_str(),_light_controls[l].energy()) ) return false ;
                    for( size_t l = 0 ; l < 2 ; ++l )
                        if( !matches(vars,("u_direction_"+motor::to_string(l)).c_str(),_directional[l]->get_transformed_direction()) ) return false ;
                }
            }
            return true ;
        }

        void_t update_variables( void_t ) noexcept
        {
            _frame_post = _post_enabled ; _frame_particles = _particles_enabled ;
            _frame_shadows = _shadows ;
            _frame_dragon = _dragon_enabled ;
            _frame_fire = _dragon_enabled && _breath ;
            float_t const flight = _time*0.16f ;
            vec4 const dragon_frame(7.0f*std::sin(flight),10.5f+0.7f*std::sin(flight*2),5.5f*std::cos(flight),
                std::atan2(7.0f*std::cos(flight),-5.5f*std::sin(flight))) ;
            for( size_t tile = 0 ; tile < _opts.tiles ; ++tile )
                _model_trafos[tile][7]->borrow_trafo_is()->set_value(motor::math::m3d::trafof_t(
                    vec3(1),vec3(0,dragon_frame.w(),0),vec3(dragon_frame))) ;
            float_t const angle = (_yaw + (_orbit ? _time*3.0f : 0.0f))*pi/180.0f ;
            vec3 const target(0,2,0), eye = target+vec3(std::sin(angle)*_distance,_height,std::cos(angle)*_distance) ;
            auto const right = (target-eye).normalized().cross(vec3(0,1,0)).normalized() ;
            auto const up = vec3(right).cross((target-eye).normalized()).normalized() ;
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto & window = _windows[w] ;
                window.camera.make_perspective_fov(float_t(window.dimensions.x())/float_t(window.dimensions.y()),1.0f,0.80f,0.1f,300.0f) ;
                window.camera.look_at(eye,vec3(0,1,0),target) ;
                auto const vp = window.camera.get_proj_matrix()*window.camera.get_view_matrix() ;
                window.matrix->set(vp) ;
                window.clock->set(vec4(_time,_emission,0,_frame_fire ? 1.0f : 0.0f)) ;
                window.dragon->set(dragon_frame) ;
                window.right->set(right) ; window.up->set(up) ;
            }
        }

        void_t open_window( size_t const w ) noexcept
        {
            motor::application::window_info_t info ;
            bool_t const d3d = _opts.d3d || w == 1 ;
            info.window_name = motor::string_t("motor | Village 1800 | ")+(d3d ? "D3D11" : "GL4")+
                " | "+motor::to_string(_opts.tiles)+" tiles | "+(_opts.detail == 0 ? "low" : _opts.detail == 1 ? "high" : "ultra")+
                (_opts.benchmark ? " | benchmark" : "")+" | "+motor::to_string(_triangles)+" triangles | "+motor::to_string(particle_count())+" particles" ;
            info.x = int_t(40+w*70) ; info.y = int_t(40+w*50) ; info.w = 1280 ; info.h = 800 ;
            _opts.capture.apply( info ) ;
            _windows[w].dimensions = motor::math::vec2ui_t( uint_t( info.w ), uint_t( info.h ) ) ;
            info.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
            _windows[w].id = create_window(info) ;
            send_window_message(_windows[w].id,[vsync = _opts.vsync]( auto & window )
            {
                window.send_message(motor::application::show_message({true})) ;
                window.send_message(motor::application::vsync_message_t({vsync})) ;
            }) ;
        }

    public:
        village_app( options const opts, bool_t * passed ) noexcept : _opts(opts), _passed(passed), _paused(opts.still || opts.benchmark) {}

#ifdef _MSC_VER
#pragma warning( push )
#pragma warning( disable : 4723 )
#endif
        void_t on_init( void_t ) noexcept override
        {
            _started = std::chrono::steady_clock::now() ;
            int_t post_width = 1920, post_height = 1080 ;
            _opts.capture.apply( post_width, post_height ) ;
            if( _opts.benchmark )
            {
                _timings.reserve(_opts.frames) ;
                for( auto & window : _windows ) window.benchmark_intervals.reserve(_opts.frames) ;
            }
            vec3 chimneys[4] ;
            {
                mesh meshes[model_count] ;
                vec3 const origins[] = {vec3(-5.8f,0,-6),vec3(5.8f,0,-6),vec3(-9,0,4.4f),vec3(9,0,4.4f)} ;
                float_t const angles[] = {0,0,pi*0.5f,-pi*0.5f} ;
                for( size_t i = 0 ; i < 4 ; ++i )
                {
                    meshes[i].origin = origins[i] ; meshes[i].angle = angles[i] ;
                    chimneys[i] = house(meshes[i],i,_opts.detail) ;
                }
                surroundings(meshes[4],meshes[5],meshes[6],_opts.detail) ;
                meshes[7] = make_dragon() ;
                village_well(meshes[8]) ; village_statue(meshes[9]) ;
                for( size_t i = 0 ; i < model_count ; ++i )
                {
                    bool_t const valid = valid_mesh(meshes[i]) ;
                    motor::log::global_t::test("[village] mesh "+motor::to_string(i)+" indices, finite positions and normals",valid) ;
                    if( !valid ) { fail("[village] generated geometry is valid") ; return ; }
                    size_t split_indices = 0 ;
                    for( size_t material = 0 ; material < size_t(material_id::count) ; ++material )
                    {
                        auto const m = material_mesh(meshes[i],material_id(material)) ;
                        if( m.indices.empty() ) continue ;
                        if( !valid_mesh(m) ) { fail("[village] material mesh is valid") ; return ; }
                        split_indices += m.indices.size() ;
                        _vertices += m.vertices.size() ;
                        auto vb = motor::graphics::vertex_buffer_t()
                            .add_layout_element(motor::graphics::vertex_attribute::position,motor::graphics::type::tfloat,motor::graphics::type_struct::vec3)
                            .add_layout_element(motor::graphics::vertex_attribute::normal,motor::graphics::type::tfloat,motor::graphics::type_struct::vec3)
                            .add_layout_element(motor::graphics::vertex_attribute::color0,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4) ;
                        vb.resize(m.vertices.size()).update<vertex>([&]( vertex * p, size_t const n )
                        { for( size_t j = 0 ; j < n ; ++j ) p[j] = m.vertices[j] ; }) ;
                        auto ib = motor::graphics::index_buffer_t().set_layout_element(motor::graphics::type::tuint) ;
                        ib.resize(m.indices.size()).update<uint_t>([&]( uint_t * p, size_t const n )
                        { for( size_t j = 0 ; j < n ; ++j ) p[j] = m.indices[j] ; }) ;
                        _geometry.emplace_back("village_mesh_"+motor::to_string(_geometry.size()),
                            motor::graphics::primitive_type::triangles,std::move(vb),std::move(ib)) ;
                        scene_object object ; object.model = i ; object.material = material_id(material) ;
                        object.geometry = _geometry.size()-1 ;
                        object.triangles = m.indices.size()/3 ;
                        _objects.emplace_back(object) ;
                    }
                    if( split_indices != meshes[i].indices.size() ) { fail("[village] material split preserves every triangle") ; return ; }
                }
            }
            {
                auto layout = motor::graphics::vertex_buffer_t()
                    .add_layout_element(motor::graphics::vertex_attribute::position,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4)
                    .add_layout_element(motor::graphics::vertex_attribute::color0,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4) ;
                _stream = motor::graphics::streamout_object_t("village_stream",std::move(layout)) ;
                _stream.resize(particle_count()) ;
                auto vb = motor::graphics::vertex_buffer_t()
                    .add_layout_element(motor::graphics::vertex_attribute::position,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4)
                    .add_layout_element(motor::graphics::vertex_attribute::color0,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4)
                    .add_layout_element(motor::graphics::vertex_attribute::texcoord0,motor::graphics::type::tfloat,motor::graphics::type_struct::vec4) ;
                mesh random ;
                vb.resize(particle_count()).update<particle_seed>([&]( particle_seed * p, size_t const n )
                {
                    size_t const per_tile = n/_opts.tiles ;
                    size_t const smoke = std::min(size_t(512),per_tile/4), sparks = std::min(size_t(1024),per_tile/4) ;
                    for( size_t i = 0 ; i < n ; ++i )
                    {
                        size_t const local = i%per_tile, tile = i/per_tile ;
                        vec3 origin ; float_t kind = 0 ;
                        if( local < smoke ) origin = chimneys[local%4]+vec3(random.random()*0.16f-0.08f,0,random.random()*0.16f-0.08f) ;
                        else if( local < smoke+sparks ) { origin = vec3(2.8f,0.6f,2.6f) ; kind = 1 ; }
                        else if( local < smoke+sparks+per_tile/8 ) { origin = vec3(0) ; kind = 3 ; }
                        else { origin = vec3(random.random()*28-14,random.random()*7+0.3f,random.random()*24-12) ; kind = 2 ; }
                        p[i] = {vec4(origin,random.random()),vec4(random.random(),random.random(),random.random(),kind),
                            vec4(tile_offset(tile),tile_angle(tile))} ;
                    }
                }) ;
                _seeds = motor::graphics::geometry_object_t("village_seeds",motor::graphics::primitive_type::points,std::move(vb)) ;
            }
            {
                motor::graphics::msl_object_t surface("village_surface",true), shadow("village_moon_shadow",true),
                    capture("village_capture"), particles("village_particles") ;
                surface.add(motor::graphics::msl_api_type::msl_4_0,make_surface_shader()) ;
                shadow.add(motor::graphics::msl_api_type::msl_4_0,make_shadow_shader()) ;
                capture.add(motor::graphics::msl_api_type::msl_4_0,capture_shader) ;
                particles.add(motor::graphics::msl_api_type::msl_4_0,particle_shader) ;
                for( size_t i = 0 ; i < _geometry.size() ; ++i )
                {
                    surface.link_geometry("village_mesh_"+motor::to_string(i)) ;
                    shadow.link_geometry("village_mesh_"+motor::to_string(i)) ;
                }
                capture.link_geometry("village_seeds") ; particles.link_geometry("village_seeds","village_stream") ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    auto & window = _windows[w] ;
                    {
                        motor::graphics::variable_set_t vars ;
                        window.clock = vars.data_variable<vec4>("u_clock") ;
                        window.dragon = vars.data_variable<vec4>("u_dragon") ;
                        capture.add_variable_set(motor::shared(std::move(vars))) ;
                    }
                    {
                        motor::graphics::variable_set_t vars ;
                        window.matrix = vars.data_variable<motor::math::mat4f_t>("u_view_projection") ;
                        window.right = vars.data_variable<vec3>("u_right") ; window.up = vars.data_variable<vec3>("u_up") ;
                        particles.add_variable_set(motor::shared(std::move(vars))) ;
                    }
                }
                _surface = motor::shared(std::move(surface)) ; _capture = motor::shared(std::move(capture)) ; _particles = motor::shared(std::move(particles)) ;
                _shadow = motor::shared(std::move(shadow)) ;
            }
            make_scene() ;
            {
                _shadow_fb = motor::graphics::framebuffer_object_t("village_moon") ;
                _shadow_fb.set_target(motor::graphics::depth_stencil_target_type::depth32).resize(2048,2048) ;
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = true ;
                s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ;
                s.clear_s.ss.do_color_clear = false ; s.clear_s.ss.do_depth_clear = true ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ; s.view_s.ss.vp = motor::math::vec4ui_t(0,0,2048,2048) ;
                _shadow_state = motor::graphics::state_object_t("village_moon_state") ;
                _shadow_state.add_render_state_set(s) ;
            }
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = true ;
                s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ;
                s.clear_s.ss.do_color_clear = true ; s.clear_s.ss.do_depth_clear = true ;
                s.clear_s.ss.clear_color = vec4(0.06f,0.10f,0.15f,1) ;
                _opaque_state = motor::graphics::state_object_t("village_opaque") ; _opaque_state.add_render_state_set(s) ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
                s.view_s.ss.vp = motor::math::vec4ui_t(0,0,uint_t(post_width),uint_t(post_height)) ;
                _hdr_state = motor::graphics::state_object_t("village_hdr") ; _hdr_state.add_render_state_set(s) ;
            }
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = true ; s.depth_s.ss.do_depth_write = false ;
                s.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = true ;
                s.blend_s.ss.blend_func = motor::graphics::blend_function::add ;
                s.blend_s.ss.src_blend_factor = motor::graphics::blend_factor::src_alpha ;
                s.blend_s.ss.dst_blend_factor = motor::graphics::blend_factor::one_minus_src_alpha ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = false ;
                _particle_state = motor::graphics::state_object_t("village_particle_state") ; _particle_state.add_render_state_set(s) ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = false ; s.depth_s.ss.do_depth_write = false ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = false ;
                s.view_s.do_change = true ; s.view_s.ss.do_activate = true ;
                s.view_s.ss.vp = motor::math::vec4ui_t(0,0,_windows[w].dimensions.x(),_windows[w].dimensions.y()) ;
                _windows[w].present = motor::graphics::state_object_t("village_present_"+motor::to_string(w)) ;
                _windows[w].present.add_render_state_set(s) ;
            }
            _post = new( motor::memory::global_t::create( sizeof( motor::gfx::hdr_postprocess_pipeline_t ) ) )
                motor::gfx::hdr_postprocess_pipeline_t( uint_t( post_width ), uint_t( post_height ) ) ;
            _post->init() ;
            {
                auto properties = _post->property_sheets() ;
                properties.at("brightpass")->borrow_property<motor::wire::input_slot<float_t>>("brightness_threshold")->set(1.1f) ;
                _post->update_properies() ;
            }
            update_variables() ;
            update_scene() ;
            _smoke_moon_view = _moon_shadow.view ;
            motor::log::global_t::status("[village] "+motor::to_string(_opts.tiles)+" tiles, "+motor::to_string(_vertices)+" shared vertices, "+motor::to_string(_triangles)+
                " triangles per scene pass, "+motor::to_string(_village_draws)+" village + "+motor::to_string(_dragon_draws)+
                " dragon material batches, "+motor::to_string(particle_count())+" GPU particle seeds") ;
            for( size_t w = 0 ; w < window_count() ; ++w ) open_window(w) ;
        }
#ifdef _MSC_VER
#pragma warning( pop )
#endif

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _quitting ) return ;
            auto const started = std::chrono::steady_clock::now() ;
            _benchmark_rendered = 0 ; _prepare_ms = 0 ;
            if( !_paused )
            {
                _time += data.sec_dt ;
                if( _moon_orbit )
                {
                    _moon_yaw = std::fmod(_moon_yaw+_moon_speed*data.sec_dt,360.0f) ;
                    if( _moon_yaw > 180.0f ) _moon_yaw -= 360.0f ;
                    if( _moon_yaw < -180.0f ) _moon_yaw += 360.0f ;
                }
            }
            update_variables() ;
            update_scene() ;
            // These properties are populated by the stages' compilation listeners, not init().
            auto properties = _post->property_sheets() ;
            if( auto * p = properties.at("merge")->borrow_property<motor::wire::input_slot<float_t>>("strength_b") ) p->set(0.25f) ;
            if( auto * p = properties.at("tone_map")->borrow_property<motor::wire::input_slot<vec3>>("reinhard_offset") ) p->set(vec3(1.0f)) ;
            _graphics_ms = elapsed_ms(started) ;
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto & geo : _geometry ) fe->configure<motor::graphics::geometry_object_t>(&geo) ;
            fe->configure<motor::graphics::geometry_object_t>(&_seeds) ; fe->configure<motor::graphics::streamout_object_t>(&_stream) ;
            fe->configure<motor::graphics::framebuffer_object_t>(&_shadow_fb) ;
            for( auto * state : {&_opaque_state,&_hdr_state,&_particle_state,&_shadow_state} ) fe->configure<motor::graphics::state_object_t>(state) ;
            for( auto * shader : {_surface,_shadow,_capture,_particles} ) fe->configure<motor::graphics::msl_object_t>(shader) ;
            _post->init_render(fe) ;
        }

        void_t on_last_frame( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            _post->release_render(fe) ;
            fe->release<motor::graphics::state_object_t>(&_windows[id == _windows[0].id ? 0 : 1].present) ;
            for( auto * shader : {_surface,_shadow,_capture,_particles} ) fe->release<motor::graphics::msl_object_t>(shader) ;
            fe->release<motor::graphics::framebuffer_object_t>(&_shadow_fb) ;
            fe->release<motor::graphics::streamout_object_t>(&_stream) ; fe->release<motor::graphics::geometry_object_t>(&_seeds) ;
            for( auto & geo : _geometry ) fe->release<motor::graphics::geometry_object_t>(&geo) ;
            for( auto * state : {&_opaque_state,&_hdr_state,&_particle_state,&_shadow_state} ) fe->release<motor::graphics::state_object_t>(state) ;
        }

        void_t on_render( window_id_t const id, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            auto const started = std::chrono::steady_clock::now() ;
            size_t const w = id == _windows[0].id ? 0 : 1 ; auto & window = _windows[w] ;
            window.draws = draw_counts() ;
            if( window.viewport_dirty )
            {
                window.present.access_render_state(0,[&]( motor::graphics::render_state_sets_ref_t s )
                { s.view_s.ss.vp = motor::math::vec4ui_t(0,0,window.dimensions.x(),window.dimensions.y()) ; return true ; }) ;
                fe->configure<motor::graphics::state_object_t>(&window.present) ; window.viewport_dirty = false ;
            }
            bool_t ready = true ;
            for( auto * shader : {_surface,_shadow,_capture,_particles} )
            {
                auto const status = fe->decode(shader) ;
                ready &= status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
                if( status.second == motor::graphics::result::failed ) { fail("[village] shader configuration"); return ; }
            }
            auto const shadow_status = fe->decode(&_shadow_fb) ;
            if( shadow_status.second == motor::graphics::result::failed ) { fail("[village] shadow framebuffer"); return ; }
            ready &= shadow_status.first == motor::graphics::object_state::ready && shadow_status.second == motor::graphics::result::ok ;
            window.ready = ready ;
            auto const pipeline = _post->check_status(fe) ;
            if( pipeline.state == motor::gfx::postprocess_status_t::state_type::failed ) { fail("[village] HDR pipeline"); return ; }
            if( !ready || !_scene_ready || _quitting || (_frame_post && pipeline.state != motor::gfx::postprocess_status_t::state_type::ready) ) return ;
            using backend = motor::graphics::gen4::backend ;
            // Cache subset IDs once during warmup; do not add smoke work to measured frames.
            bool_t const inspect_subsets = _opts.smoke || (_opts.benchmark && window.frames == _opts.warmup-1) ;
            if( inspect_subsets ) for( auto & object : _objects )
                object.shadow_subset[w] = object.shadow_msl->render_update(w,&_moon_camera) ;
            // Also clear when disabled: color shaders always have a valid depth texture.
            fe->use(&_shadow_fb) ; fe->push(&_shadow_state) ;
            if( _frame_shadows )
            {
                motor::scene::render_visitor_t visitor(1,w,fe,&_moon_camera) ;
                motor::scene::node_t::traverser(_village).apply(&visitor) ;
                window.draws.shadow = _village_draws ;
                if( _frame_dragon )
                {
                    motor::scene::node_t::traverser(_dragon_node).apply(&visitor) ;
                    window.draws.shadow += _dragon_draws ;
                }
                ++window.shadow_frames ;
            }
            else ++window.unshadowed_frames ;
            fe->pop(backend::pop_type::render_state) ; fe->unuse(backend::unuse_type::framebuffer) ;
            if( _frame_particles )
            {
                motor::graphics::gen4::backend::render_detail_t d ; d.varset = w ; d.num_elems = particle_count() ;
                fe->use(&_stream) ; fe->render(_capture,d) ; fe->unuse(backend::unuse_type::streamout) ;
                ++window.draws.capture ;
            }
            if( _frame_post ) fe->use(_post->borrow_hdr_fb(0)) ;
            fe->push(_frame_post ? &_hdr_state : &_opaque_state) ;
            if( inspect_subsets )
            {
                for( auto & object : _objects ) object.subset[w] = object.msl->render_update(w,&window.camera) ;
            }
            {
                motor::scene::light_pass_render_visitor_t visitor(0,w,fe,&window.camera,_directional[0],&_moon_shadow) ;
                motor::scene::node_t::traverser(_village).apply(&visitor) ;
                // One ready msl leaf per object/material pair.
                window.draws.scene = _village_draws ;
            }
            if( _frame_dragon )
            {
                motor::scene::light_pass_render_visitor_t visitor(0,w,fe,&window.camera,_directional[0],&_moon_shadow) ;
                motor::scene::node_t::traverser(_dragon_node).apply(&visitor) ;
                window.draws.scene += _dragon_draws ;
                ++window.dragon_frames ;
            }
            else ++window.hidden_dragon_frames ;
            if( _frame_particles )
            {
                fe->push(&_particle_state) ;
                backend::render_detail_t d ; d.varset = w ; d.feed_from_streamout = true ; d.use_streamout_count = true ;
                fe->render(_particles,d) ; fe->pop(backend::pop_type::render_state) ; ++window.particle_frames ;
                ++window.draws.particles ;
                if( _frame_fire ) ++window.fire_frames ;
            }
            fe->pop(backend::pop_type::render_state) ;
            if( _frame_post )
            {
                fe->unuse(backend::unuse_type::framebuffer) ;
                fe->push(&window.present) ; _post->render(fe) ; fe->pop(backend::pop_type::render_state) ; ++window.post_frames ;
                // Current HDR pipeline: brightpass + 4 down + 4 up + merge + tone + FXAA + present.
                window.draws.post = 13 ;
            }
            ++window.frames ;
            window.benchmark_stamp = std::chrono::steady_clock::now() ;
            _prepare_ms += elapsed_ms(started) ; ++_benchmark_rendered ;
        }

        bool_t on_tool( window_id_t const id, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( id != _windows[0].id || _quitting || _opts.smoke || _opts.benchmark ) return false ;
            ImGui::SetNextWindowPos(ImVec2(12,12),ImGuiCond_FirstUseEver) ;
            ImGui::SetNextWindowSize(ImVec2(350,0),ImGuiCond_FirstUseEver) ;
            float_t const panel_height = ImGui::GetIO().DisplaySize.y-24.0f ;
            ImGui::SetNextWindowSizeConstraints(ImVec2(300,200),ImVec2(500,panel_height > 200 ? panel_height : 200)) ;
            if( ImGui::Begin("Village 1800",nullptr,ImGuiWindowFlags_NoSavedSettings) )
            {
                ImGui::Text("Triangles: %llu",static_cast<unsigned long long>(_triangles)) ;
                ImGui::Text("Particles: %llu",static_cast<unsigned long long>(particle_count())) ;
                ImGui::Text("Scene: %u tiles | %u houses | %u dragons",uint_t(_opts.tiles),uint_t(4*_opts.tiles),uint_t(_opts.tiles)) ;
                ImGui::Text("%u wells | %u statues | quarter-turn tiles",uint_t((_opts.tiles+1)/2),uint_t(_opts.tiles/2)) ;
                ImGui::Text("%llu shared meshes | %llu material draws",static_cast<unsigned long long>(_geometry.size()),static_cast<unsigned long long>(_objects.size())) ;
                ImGui::Text("Materials: %llu | Lights: %llu",static_cast<unsigned long long>(size_t(material_id::count)),static_cast<unsigned long long>(_collected_lights)) ;
                if( ImGui::TreeNode("Material draws (dragon enabled)") )
                {
                    for( size_t i = 0 ; i < size_t(material_id::count) ; ++i )
                        ImGui::Text("%s: %llu",materials[i].name,static_cast<unsigned long long>(_material_draws[i])) ;
                    ImGui::TreePop() ;
                }
                ImGui::Text("%.1f ms / %.0f FPS",1000.0f/ImGui::GetIO().Framerate,ImGui::GetIO().Framerate) ;
                for( size_t w = 0 ; w < window_count() ; ++w )
                {
                    auto const & d = _windows[w].draws ;
                    ImGui::Text("%s draw calls (no UI): %u",_opts.d3d || w == 1 ? "D3D11" : "GL4",d.total()) ;
                    ImGui::Text("Scene %u | Capture %u | Particles %u",d.scene,d.capture,d.particles) ;
                    ImGui::Text("Moon shadow: %u",d.shadow) ;
                    ImGui::Text("Post pipeline: %u",d.post) ;
                }
                ImGui::Separator() ;
                ImGui::SliderFloat("Yaw",&_yaw,-180,180) ; ImGui::SliderFloat("Distance",&_distance,15,140) ;
                ImGui::SliderFloat("Height",&_height,5,90) ; ImGui::Checkbox("Orbit",&_orbit) ;
                ImGui::Checkbox("Pause",&_paused) ; ImGui::Checkbox("Particles",&_particles_enabled) ;
                ImGui::Checkbox("Dragon",&_dragon_enabled) ; ImGui::Checkbox("Fire breath",&_breath) ;
                ImGui::Separator() ;
                ImGui::SliderFloat("Moon yaw",&_moon_yaw,-180,180,"%.1f deg") ;
                ImGui::SliderFloat("Moon elevation",&_moon_elevation,10,80,"%.1f deg") ;
                ImGui::SliderFloat("Moon speed",&_moon_speed,-15,15,"%.1f deg/s") ;
                ImGui::Checkbox("Moon orbit",&_moon_orbit) ;
                ImGui::Checkbox("Moon shadows",&_shadows) ; ImGui::SliderFloat("Shadow bias",&_shadow_bias,0.0f,0.002f,"%.5f") ;
                if( ImGui::TreeNodeEx("Light colors / intensity",ImGuiTreeNodeFlags_DefaultOpen) )
                {
                    char_cptr_t const names[] = {"Moon","Sky fill","Fire"} ;
                    for( size_t l = 0 ; l < 3 ; ++l )
                    {
                        ImGui::PushID(int_t(l)) ;
                        ImGui::TextUnformatted(names[l]) ;
                        auto & controls = _light_controls[l] ;
                        ImGui::ColorEdit3("Color (linear)",controls.color,ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float) ;
                        ImGui::SliderFloat("Intensity",&controls.intensity,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp) ;
                        if( ImGui::Button("Reset") )
                        {
                            motor::gfx::light_mtr_t light = l < 2 ?
                                static_cast<motor::gfx::light_mtr_t>(_directional[l]) : _fire_light[0] ;
                            controls.reset(light) ;
                        }
                        ImGui::PopID() ;
                    }
                    ImGui::TreePop() ;
                }
                ImGui::Checkbox("HDR / bloom / FXAA",&_post_enabled) ; ImGui::SliderFloat("Emission",&_emission,0,3) ;
            }
            ImGui::End() ; return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( _opts.benchmark && !_quitting ) { benchmark_frame() ; return ; }
            if( !_opts.smoke || _quitting ) return ;
            for( size_t w = 0 ; w < window_count() ; ++w )
                if( _windows[w].frames < (_smoke_phase+1)*40 ) return ;
            if( !verify_scene() ) { fail("[village] scene transforms, camera subsets, collector and wire bridges") ; return ; }
            bool_t moon_moved = false ;
            for( size_t i = 0 ; i < 16 ; ++i )
                moon_moved |= std::abs(_moon_shadow.view[i]-_smoke_moon_view[i]) > 0.0001f ;
            bool_t const expect_motion = _smoke_phase == 2 || (!_opts.still && _smoke_phase != 1) ;
            if( moon_moved != expect_motion ) { fail("[village] moon orbit and frozen shadow camera") ; return ; }
            _smoke_moon_view = _moon_shadow.view ;
            if( _smoke_phase == 0 )
            {
                _light_controls[0].color[0] = 0.2f ; _light_controls[0].intensity = 2.0f ;
                _light_controls[1].intensity = 0.0f ;
                _light_controls[2].color[2] = 1.7f ;
                _breath = false ; _shadows = false ; _moon_orbit = false ; ++_smoke_phase ; return ;
            }
            if( _smoke_phase == 1 )
            {
                _dragon_enabled = false ; _shadows = true ;
                _light_controls[1].color[1] = 0.65f ; _light_controls[1].intensity = 1.5f ;
                _light_controls[2].intensity = 0.0f ;
                _moon_yaw += 90.0f ; _moon_elevation = 25.0f ; _moon_orbit = true ;
                ++_smoke_phase ; return ;
            }
            if( _smoke_phase == 2 )
            {
                for( size_t l = 0 ; l < 2 ; ++l ) _light_controls[l].reset(_directional[l]) ;
                _light_controls[2].reset(_fire_light[0]) ;
                _dragon_enabled = true ; _breath = true ; ++_smoke_phase ; return ;
            }
            for( size_t w = 0 ; w < window_count() ; ++w )
            {
                auto const & window = _windows[w] ;
                if( window.post_frames < 160 || window.particle_frames < 160 || window.dragon_frames < 80 ||
                    window.hidden_dragon_frames < 20 || window.fire_frames < 40 || window.shadow_frames < 100 || window.unshadowed_frames < 20 )
                { fail("[village] dragon/fire toggle submission counters") ; return ; }
                if( window.draws.scene != _village_draws+_dragon_draws || window.draws.shadow != window.draws.scene ||
                    window.draws.capture != 1 || window.draws.particles != 1 || window.draws.post != 13 )
                { fail("[village] draw-call accounting") ; return ; }
            }
            motor::log::global_t::test("[village] tile count, shared geometry, material parameters, collected lights, wire bridges and per-view subsets",true) ;
            motor::log::global_t::test("[village] quarter-turn tile transforms, rotated fire lights and selected centerpieces",true) ;
            motor::log::global_t::test("[village] 160 HDR + transform-feedback frames, moon shadow matrices and shadow/dragon/fire toggles on every backend",true) ;
            motor::log::global_t::test("[village] moon orbit, stop/resume, manual angles and matching light/shadow subsets",true) ;
            motor::log::global_t::test("[village] light colors, HDR values, zero intensity and reset propagated to every view subset",true) ;
            *_passed = true ; _quitting = true ; close() ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( (_opts.smoke || _opts.benchmark) && !_quitting && std::chrono::steady_clock::now()-_started > std::chrono::seconds(180) )
                fail("[village] automated run completed before timeout") ;
        }

        void_t on_event( window_id_t const id, motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        {
            if( s.resize_changed && s.resize_msg.resize && s.resize_msg.w > 0 && s.resize_msg.h > 0 )
            {
                auto & window = _windows[id == _windows[0].id ? 0 : 1] ;
                window.dimensions = motor::math::vec2ui_t(uint_t(s.resize_msg.w),uint_t(s.resize_msg.h)) ; window.viewport_dirty = true ;
            }
            if( s.close_changed ) { _quitting = true ; close() ; }
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            _light_collector.clear_lights() ;
            motor::release(motor::move(_root)) ;
            _village = nullptr ; _dragon_node = nullptr ; _controls = nullptr ;
            for( auto & object : _objects ) object = scene_object() ;
            for( auto & tile : _model_trafos ) for( auto & trafo : tile ) trafo = nullptr ;
            motor::release(motor::move(_surface)) ; motor::release(motor::move(_capture)) ; motor::release(motor::move(_particles)) ;
            motor::release(motor::move(_shadow)) ;
            motor::release(motor::move(_post)) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    village::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--gl-only" ) { opts.dual = false ; opts.d3d = false ; }
        else if( arg == "--youtube-short" && i + 1 < argc && opts.capture.parse( argv[i+1] ) ) ++i ;
        else if( arg == "--d3d-only" ) { opts.dual = false ; opts.d3d = true ; }
        else if( arg == "--dual" ) { opts.dual = true ; opts.d3d = false ; }
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--benchmark" ) opts.benchmark = true ;
        else if( arg == "--sleep-probe" ) opts.sleep_probe = true ;
        else if( arg == "--cpu-sleep-us" && i+1 < argc )
        {
            char * end = nullptr ; auto const value = std::strtoul(argv[++i],&end,10) ;
            if( !end || end == argv[i] || *end != '\0' || value > 30 ) return 2 ;
            opts.cpu_sleep_us = size_t(value) ;
        }
        else if( arg == "--no-vsync" ) opts.vsync = false ;
        else if( arg == "--still" ) opts.still = true ;
        else if( arg == "--detail" && i+1 < argc )
        {
            motor::string_t const value(argv[++i]) ;
            if( value == "low" ) opts.detail = 0 ; else if( value == "high" ) opts.detail = 1 ; else if( value == "ultra" ) opts.detail = 2 ; else return 2 ;
        }
        else if( arg == "--particles" && i+1 < argc )
        {
            char * end = nullptr ; auto const value = std::strtoul(argv[++i],&end,10) ;
            if( !end || *end != '\0' || value < 256 || value > 65536 ) return 2 ; opts.particles = size_t(value) ;
        }
        else if( (arg == "--tiles" || arg == "--total-particles" || arg == "--warmup" || arg == "--frames") && i+1 < argc )
        {
            char * end = nullptr ; auto const value = std::strtoul(argv[++i],&end,10) ;
            if( !end || end == argv[i] || *end != '\0' ) return 2 ;
            if( arg == "--tiles" ) { if( value != 1 && value != 4 ) return 2 ; opts.tiles = size_t(value) ; }
            else if( arg == "--total-particles" )
            { if( value < 1024 || value > 262144 || value%4 != 0 ) return 2 ; opts.total_particles = size_t(value) ; }
            else
            {
                if( value < 10 || value > 10000 ) return 2 ;
                if( arg == "--warmup" ) opts.warmup = size_t(value) ; else opts.frames = size_t(value) ;
            }
        }
        else
        {
            motor::log::global_t::status("Usage: 15_village [--gl-only|--d3d-only|--dual] [--detail low|high|ultra] [--tiles 1|4] [--particles 256..65536] [--total-particles 1024..262144 (multiple of 4)] [--still] [--smoke] [--no-vsync] [--benchmark] [--warmup 10..10000] [--frames 10..10000] [--cpu-sleep-us 0..30] [--sleep-probe (benchmark only)] [--youtube-short square|vertical]") ;
            motor::log::global_t::deinit() ; return arg == "--help" ? 0 : 2 ;
        }
    }
    if( opts.benchmark )
    {
        if( opts.smoke ) return 2 ;
        opts.vsync = false ;
        if( opts.total_particles == 0 ) opts.total_particles = 32768 ;
    }
#ifndef _WIN32
    if( opts.dual || opts.d3d ) return 2 ;
#endif
    if( opts.sleep_probe && !opts.benchmark ) return 2 ;
    motor::application::carrier::set_cpu_sleep(std::chrono::microseconds(opts.cpu_sleep_us)) ;
    motor::log::global_t::status<128>("VILLAGE_SLEEP cpu_sleep_us=%lld",
        static_cast<long long>(motor::application::carrier::get_cpu_sleep().count())) ;
    bool passed = false ; int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(village::village_app(opts,&passed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const remaining = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : (remaining != 0 || ((opts.smoke || opts.benchmark) && !passed) ? 1 : 0) ;
}
