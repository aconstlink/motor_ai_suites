#include <motor/platform/global.h>
#include <motor/format/global.h>
#include <motor/format/module_registry.hpp>
#include <motor/format/future_items.hpp>
#include <motor/io/database.h>
#include <motor/io/global.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/scene/node/logic_group.h>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/graphics/msl_set_component.hpp>
#include <motor/scene/visitor/default_visitor.h>
#include <motor/scene/visitor/variable_update_visitor.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/tool/imgui/imgui.h>
#include <motor/concurrent/global.h>
#include <motor/profiling/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <algorithm>

namespace this_file
{
    using namespace motor::core::types ;
    using vec3 = motor::math::vec3f_t ;
    using vec4 = motor::math::vec4f_t ;
    using clock = std::chrono::steady_clock ;

    struct options
    {
        bool_t d3d = false, smoke = false, close_loading = false ;
        motor::string_t assets = MOTOR_ASYNC_ASSETS ;
    } ;

    struct asset
    {
        motor::format::future_item_t future ;
        motor::scene::logic_group_mtr_t root = nullptr ;
        // Borrowed from the scene; never used after root is released.
        motor::vector<motor::scene::msl_component_mtr_t> components ;
        motor::vector<motor::graphics::msl_object_mtr_t> shaders ;
        clock::time_point started ;
        double load_ms = 0 ;
        size_t pending_frames = 0, visible_frames = 0 ;
        bool_t requested = false, gpu_ready = false ;
    } ;

    struct collect_visitor : motor::scene::default_visitor
    {
        asset & target ;
        bool_t suzanne ;
        collect_visitor( asset & a, bool_t const s ) noexcept : target(a), suzanne(s) {}
        motor::scene::result visit( motor::scene::node_ptr_t node ) noexcept override
        {
            if( auto * set = node->borrow_component<motor::scene::msl_set_component_t>() )
            {
                set->for_each_msl([&]( motor::scene::msl_component_mtr_t comp )
                {
                    target.components.emplace_back(comp) ;
                    auto * shader = comp->borrow_msl() ;
                    if( std::find(target.shaders.begin(),target.shaders.end(),shader) == target.shaders.end() )
                    {
                        target.shaders.emplace_back(shader) ;
                        // Replace only shader source before first configure; keep links and sets.
                        // Normal colors make the texture-free geometry readable without lights.
                        shader->clear_shaders().add(motor::graphics::msl_api_type::msl_4_0,
                            "config "+shader->name()+R"(
                            {
                                vertex_shader
                                {
                                    mat4_t proj : projection ;
                                    mat4_t view : view ;
                                    mat4_t world : world ;
                                    in vec3_t pos : position ;
                                    in vec3_t nrm : normal ;
                                    out vec4_t pos : position ;
                                    out vec3_t nrm : normal ;
                                    void main()
                                    {
                                        out.pos = proj * view * world * vec4_t(in.pos,1.0) ;
                                        out.nrm = in.nrm ;
                                    }
                                }
                                pixel_shader
                                {
                                    vec4_t base_color ;
                                    in vec3_t nrm : normal ;
                                    out vec4_t color : color0 ;
                                    void main()
                                    {
                                        vec3_t normal_color = normalize(in.nrm) * 0.5 + vec3_t(0.5,0.5,0.5) ;
                                        vec3_t tint = vec3_t(0.5,0.5,0.5) + base_color.xyz * 0.5 ;
                                        out.color = vec4_t(normal_color.x * tint.x,
                                            normal_color.y * tint.y,normal_color.z * tint.z,1.0) ;
                                    }
                                }
                            })") ;
                    }
                    if( suzanne )
                    {
                        auto * color = comp->borrow_shader_inputs().borrow_by_cast<motor::wire::input_slot<vec4>>("base_color") ;
                        if( color ) color->set_value(vec4(0.95f,0.25f,0.06f,1)) ;
                    }
                }) ;
            }
            return motor::scene::result::ok ;
        }
        motor::scene::result post_visit( motor::scene::node_ptr_t, motor::scene::result const r ) noexcept override { return r ; }
    } ;

    class app : public motor::application::app
    {
        options _opts ;
        bool_t * _passed ;
        motor::io::database_mtr_t _db = nullptr ;
        motor::format::module_registry_mtr_t _registry = nullptr ;
        asset _assets[2] ;
        motor::gfx::generic_camera_t _camera ;
        motor::graphics::state_object_t _state ;
        motor::math::vec2ui_t _size = motor::math::vec2ui_t(1280,800) ;
        bool_t _resize = true, _closing = false, _orbit = false ;
        float_t _yaw = 90, _distance = 8, _height = 1.2f, _time = 0 ;
        size_t _frames = 0, _both_frames = 0 ;
        clock::time_point _started ;

        void_t fail( motor::string_cref_t message ) noexcept
        {
            *_passed = false ;
            motor::log::global_t::test(message,false) ;
            _closing = true ; close() ;
        }

        void_t request( size_t const i ) noexcept
        {
            auto & a = _assets[i] ;
            char_cptr_t const names[] = {"Sponza","Suzanne"} ;
            auto const name = motor::string_t(names[i]) ;
            auto props = motor::shared(motor::property::property_sheet_t()) ;
            props->set_value<motor::string_t>("base_name","async_"+name) ;
            a.started = clock::now() ; a.requested = true ;
            a.future = _registry->import_from(motor::io::location_t(name+"."+name+".gltf"),_db,motor::move(props)) ;
            motor::log::global_t::status("[async assets] requested "+name) ;
        }

        void_t poll( size_t const i ) noexcept
        {
            auto & a = _assets[i] ;
            if( !a.future.valid() ) return ;
            if( a.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready )
            {
                ++a.pending_frames ; return ;
            }
            motor::format::item_mtr_t item = nullptr ;
            try { item = a.future.get() ; }
            catch( std::exception const & e ) { fail(motor::string_t("[async assets] import: ")+e.what()) ; return ; }
            auto * scene = dynamic_cast<motor::format::scene_item_ptr_t>(item) ;
            if( !scene || !scene->root )
            {
                if( item ) { item->release() ; motor::release(motor::move(item)) ; }
                fail("[async assets] importer returned no scene") ; return ;
            }
            // Preserve imported local transforms; place Suzanne with an additional parent.
            a.root = motor::shared(motor::scene::logic_group_t()) ;
            motor::math::m3d::trafof_t placement ;
            if( i == 1 ) placement.set_translation(vec3(0,1.4f,0)) ;
            a.root->add_component(motor::shared(motor::scene::trafo3d_component_t(placement))) ;
            a.root->add_child(motor::move(scene->root)) ;
            item->release() ; motor::release(motor::move(item)) ;
            collect_visitor visitor(a,i == 1) ;
            motor::scene::node_t::traverser(a.root).apply(&visitor) ;
            a.load_ms = std::chrono::duration<double,std::milli>(clock::now()-a.started).count() ;
            if( a.components.empty() ) { fail("[async assets] imported scene contains no drawable meshes") ; return ; }
            motor::log::global_t::status("[async assets] imported "+motor::to_string(i)+": "+
                motor::to_string(a.components.size())+" primitives, "+motor::to_string(a.load_ms)+" ms") ;
        }

    public:
        app( options opts, bool_t * passed ) noexcept : _opts(std::move(opts)), _passed(passed) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = clock::now() ;
            for( auto const name : {"Sponza","Suzanne"} )
            {
                auto const folder = std::filesystem::path(_opts.assets.c_str())/name ;
                if( !std::filesystem::exists(folder/(motor::string_t(name)+".gltf").c_str()) ||
                    !std::filesystem::exists(folder/(motor::string_t(name)+".bin").c_str()) )
                { fail("[async assets] missing bundled glTF/bin files in "+_opts.assets) ; return ; }
            }
            _db = motor::shared(motor::io::database_t(motor::io::path_t(_opts.assets.c_str()),"./","async_assets")) ;
            _registry = motor::format::global::register_default_modules(motor::shared(motor::format::module_registry_t())) ;
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ; states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.polygon_s.do_change = true ; states.polygon_s.ss.do_activate = false ;
                states.clear_s.do_change = true ; states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ; states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = vec4(0.04f,0.055f,0.075f,1) ;
                states.view_s.do_change = true ; states.view_s.ss.do_activate = true ;
                states.view_s.ss.vp = motor::math::vec4ui_t(0,0,1280,800) ;
                _state = motor::graphics::state_object_t("async_asset_state") ;
                _state.add_render_state_set(states) ;
            }
            {
                motor::application::window_info_t info ;
                info.window_name = _opts.d3d ? "motor | Async Sponza + Suzanne | D3D11" : "motor | Async Sponza + Suzanne | GL4" ;
                info.x = 60 ; info.y = 60 ; info.w = 1280 ; info.h = 800 ;
                info.gen = _opts.d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                auto const id = create_window(info) ;
                send_window_message(id,[]( auto & w )
                {
                    w.send_message(motor::application::show_message({true})) ;
                    w.send_message(motor::application::vsync_message_t({true})) ;
                }) ;
            }
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t data ) noexcept override
        {
            if( _closing || !_registry ) return ;
            _time += data.sec_dt ;
            if( _orbit ) _yaw = std::fmod(_yaw+data.sec_dt*8.0f,360.0f) ;
            float_t const angle = _yaw*3.14159265f/180.0f ;
            vec3 const target(0,1.4f,0) ;
            _camera.make_perspective_fov(float_t(_size.x())/float_t(_size.y()),1,0.85f,0.1f,100.0f) ;
            _camera.look_at(target+vec3(std::sin(angle)*_distance,_height,std::cos(angle)*_distance),vec3(0,1,0),target) ;
            if( _frames >= 3 && !_assets[0].requested )
            {
                request(0) ;
                if( _opts.close_loading ) { request(1) ; _closing = true ; close() ; return ; }
            }
            // Start Suzanne while Sponza is already being displayed.
            if( _assets[0].visible_frames >= 20 && !_assets[1].requested ) request(1) ;
            for( size_t i = 0 ; i < 2 ; ++i )
            {
                poll(i) ;
                auto & a = _assets[i] ;
                if( !a.root ) continue ;
                for( auto * comp : a.components ) comp->render_update(&_camera) ;
                motor::scene::variable_update_visitor_t variables ;
                motor::scene::node_t::traverser(a.root).apply(&variables) ;
                motor::scene::trafo_visitor_t transforms ;
                motor::scene::node_t::traverser(a.root).apply(&transforms) ;
            }
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        { fe->configure<motor::graphics::state_object_t>(&_state) ; }

        void_t on_render( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            if( _closing ) return ;
            if( _resize )
            {
                _state.access_render_state(0,[&]( motor::graphics::render_state_sets_ref_t s )
                { s.view_s.ss.vp = motor::math::vec4ui_t(0,0,_size.x(),_size.y()) ; return true ; }) ;
                fe->configure<motor::graphics::state_object_t>(&_state) ; _resize = false ;
            }
            fe->push(&_state) ;
            for( auto & a : _assets )
            {
                if( !a.root ) continue ;
                a.gpu_ready = true ;
                for( auto * shader : a.shaders )
                {
                    auto const status = fe->decode(shader) ;
                    if( status.second == motor::graphics::result::failed ) { fail("[async assets] shader/render-object configuration failed") ; break ; }
                    a.gpu_ready &= status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
                }
                motor::scene::render_visitor_t visitor(fe,&_camera) ;
                motor::scene::node_t::traverser(a.root).apply(&visitor) ;
                if( a.gpu_ready ) ++a.visible_frames ;
            }
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
            ++_frames ;
            if( _assets[0].gpu_ready && _assets[1].gpu_ready ) ++_both_frames ;
        }

        bool_t on_tool( window_id_t const, motor::application::app::tool_data_ref_t ) noexcept override
        {
            if( _opts.smoke || _closing ) return false ;
            ImGui::SetNextWindowSize(ImVec2(360,0),ImGuiCond_FirstUseEver) ;
            if( ImGui::Begin("Async assets") )
            {
                ImGui::Text("Frame %llu | %.1f s",static_cast<unsigned long long>(_frames),double(_time)) ;
                char_cptr_t const names[] = {"Sponza","Suzanne"} ;
                for( size_t i = 0 ; i < 2 ; ++i )
                {
                    auto const & a = _assets[i] ;
                    ImGui::Text("%s: %s",names[i],!a.requested ? "Queued" : a.future.valid() ? "Importing" : a.gpu_ready ? "Ready" : "Configuring graphics") ;
                    ImGui::Text("%.1f ms | %llu primitives",a.load_ms,static_cast<unsigned long long>(a.components.size())) ;
                }
                ImGui::Separator() ;
                ImGui::SliderFloat("Yaw",&_yaw,-180,180) ;
                ImGui::SliderFloat("Distance",&_distance,2,15) ;
                ImGui::SliderFloat("Height",&_height,0.1f,7) ;
                ImGui::Checkbox("Orbit",&_orbit) ;
            }
            ImGui::End() ; return true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( _closing || !_opts.smoke || _both_frames < 90 ) return ;
            bool_t const ok = _assets[0].components.size() > 1 && !_assets[1].components.empty() &&
                _assets[0].visible_frames > _assets[1].visible_frames &&
                !_assets[0].future.valid() && !_assets[1].future.valid() ;
            *_passed = motor::log::global_t::test("[async assets] both imported scenes rendered for 90 frames; Suzanne added to live Sponza",ok) ;
            motor::log::global_t::status("[async assets] non-ready import polls: "+motor::to_string(_assets[0].pending_frames)+
                " / "+motor::to_string(_assets[1].pending_frames)) ;
            _closing = true ; close() ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( (_opts.smoke || _opts.close_loading) && !_closing && clock::now()-_started > std::chrono::seconds(90) )
                fail("[async assets] timeout") ;
        }

        void_t on_event( window_id_t const, motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        {
            if( s.resize_changed && s.resize_msg.resize && s.resize_msg.w > 0 && s.resize_msg.h > 0 )
            { _size = motor::math::vec2ui_t(uint_t(s.resize_msg.w),uint_t(s.resize_msg.h)) ; _resize = true ; }
            if( s.close_changed ) { _closing = true ; close() ; }
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto & a : _assets ) for( auto * shader : a.shaders ) fe->release<motor::graphics::msl_object_t>(shader) ;
            fe->release<motor::graphics::state_object_t>(&_state) ;
            // Imported geometry is owned by config components; backend teardown frees GPU buffers.
            // Keep the graph alive until on_shutdown, after the window's render thread has stopped.
        }

        void_t on_shutdown( void_t ) noexcept override
        {
            // Importer borrows the database. Drain pending results before releasing it.
            // This may wait during shutdown, never during the active frame loop.
            size_t drained = 0 ;
            bool_t imports_ok = true ;
            for( auto & a : _assets )
            {
                if( a.future.valid() )
                {
                    try
                    {
                        auto * item = a.future.get() ;
                        auto * scene = dynamic_cast<motor::format::scene_item_ptr_t>(item) ;
                        imports_ok &= scene != nullptr && scene->root != nullptr ;
                        ++drained ;
                        if( item ) { item->release() ; motor::release(motor::move(item)) ; }
                    }
                    catch( std::exception const & e )
                    { imports_ok = false ; *_passed = false ; motor::log::global_t::error(e.what()) ; }
                }
                a.components.clear() ; a.shaders.clear() ; motor::release(motor::move(a.root)) ;
            }
            if( _opts.close_loading )
                *_passed = motor::log::global_t::test("[async assets] both pending results drained before database teardown",imports_ok && drained == 2) ;
            motor::release(motor::move(_registry)) ; motor::release(motor::move(_db)) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    this_file::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--gl-only" ) opts.d3d = false ;
        else if( arg == "--d3d-only" ) opts.d3d = true ;
        else if( arg == "--smoke" ) opts.smoke = true ;
        else if( arg == "--close-loading" ) opts.close_loading = true ;
        else if( arg == "--assets" && i+1 < argc ) opts.assets = argv[++i] ;
        else return 2 ;
    }
#ifndef _WIN32
    if( opts.d3d ) return 2 ;
#endif
    bool const checking = opts.smoke || opts.close_loading ;
    bool passed = !checking ; int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(this_file::app(std::move(opts),&passed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const remaining = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : (remaining != 0 || !passed ? 1 : 0) ;
}
