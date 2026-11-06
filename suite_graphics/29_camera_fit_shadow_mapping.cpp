
#include <motor/platform/global.h>

#include <motor/scene/visitor/camera_collector_visitor.hpp>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/scene/visitor/light_collector_visitor.hpp>
#include <motor/scene/visitor/graphics/light_pass_render_visitor.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/variable_update_visitor.hpp>
#include <motor/scene/visitor/graphics/add_msl_to_set_visitor.hpp>
#include <motor/scene/visitor/graphics/connect_msl_slot_visitor.hpp>
#include <motor/scene/visitor/graphics/debug_graphics_visitor.hpp>

#include <motor/scene/node/logic_group.h>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/light_component.hpp>

#include <motor/gfx/primitive/primitive_render_3d.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/gfx/postprocess/hdr_postprocess_pipeline.h>
#include <motor/gfx/manager/msl_manager.h>
#include <motor/gfx/util/light.hpp>

#include <motor/math/utility/fn.hpp>
#include <motor/math/utility/angle.hpp>
#include <motor/math/animation/keyframe_sequence.hpp>

#include <motor/tool/imgui/imgui_property.h>

#include <motor/wire/slot/output_slot.h>

#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/concurrent/global.h>
#include <motor/format/global.h>

#include "capture_options.hpp"
#include "shadow_camera_fit.hpp"
#include "shadow_checks.hpp"
#include <atomic>
#include <memory>
#include <thread>
#include <filesystem>

// Sponza import, scene-bound light-camera fit and a looping courtyard camera.
// The shadow and lighting passes follow the original motor-suites example.
namespace this_file
{

enum class msl_id
{
    default_id,
    color_pass_id,
    normal_pass_id,
    light_pass_id,
    depth_pass_id,
    shadow_depth_id,
    shadow_light_id,
    shadow_accum_id,
    num_ids
};

static motor::scene::msl_set_component::id_t to_id( msl_id const id ) noexcept
{
    return motor::scene::msl_set_component::id_t( id );
}

using namespace motor::core::types;

struct options
{
    sample_support::capture_options capture;
    bool_t d3d = false, demo = false, cpu_only = false;
};

class my_app : public motor::application::app
{
    motor_this_typedefs( my_app );

    options _options;
    uint_t _width = 1280, _height = 720;
    bool_t _viewport_dirty = true, _pause = false;
    float_t _elapsed = 0.0f;
    float_t _sun_seconds = 0.0f;
    bool_t _animate_sun = true;
    motor::gfx::generic_camera_t _capture_camera;
    bool_t _capture_view = true;
    motor::graphics::state_object_t _present_state = motor::graphics::state_object_t( "shadow29.present" );

    motor::graphics::state_object_mtr_t _final_so = nullptr;
    motor::graphics::state_object_mtr_t _light_so = nullptr;

    motor::io::database_mtr_t _db = nullptr;

    motor::gfx::msl_manager_mtr_t _own_mmgr = nullptr;
    size_t _reg0 = size_t( -1 );
    motor::scene::node_mtr_t _root = nullptr;

    motor::gfx::primitive_render_3d_mtr_t _prim_render = nullptr;

  private: // post processing and shadow mapping

    motor::gfx::hdr_postprocess_pipeline_mtr_t _pp_pipe = nullptr;
    motor::property::property_sheet_t _pp_sheet;
    bool_t _show_temp_rt = false;
    int_t _temp_rt_index = 0;

    // this is where the depth of the light source is rendered to.
    // rendered from the light perspective.
    motor::graphics::framebuffer_object_mtr_t _shadow_depth_fb = nullptr;
    motor::graphics::state_object_mtr_t _shadow_depth_so = nullptr;

    motor::gfx::generic_camera_mtr_t _sun_cam = nullptr;

  private: // camera

    size_t _cam_id = size_t( -1 );
    motor::vector< std::pair< motor::string_t, motor::gfx::generic_camera_mtr_t > > _cameras;

    motor::scene::camera_collector_visitor_t _cc;

    struct camera_sequence_item
    {
        motor::math::time_ms_t start;
        motor::math::time_ms_t end;
        motor::gfx::generic_camera_mtr_t cam;
    };

    motor::vector< camera_sequence_item > _cs;

  private: // rendering properties

    bool_t _render_light = true;
    bool_t _render_normals = false;
    bool_t _render_default = false;

    bool_t _draw_debug = false;
    bool_t _draw_leafs = false;
    size_t _max_depth = 0;
    size_t _draw_depth = 0;

    bool_t _draw_sun_ortho = false;

    motor::collide::aabb3f_t _world_bounds;

  private: // wire

    using os_float_t = motor::wire::output_slot< float_t >;
    using os_trafo_t = motor::wire::output_slot< motor::math::m3d::trafof_t >;

    // output slot for the imported scene animations.
    os_float_t * _time = nullptr;

    // allows to scale the whole scene. it is connected
    // to the root transformation in the imported sceen.
    os_trafo_t * _scale_os = nullptr;

    motor::wire::time_node_mtr_t _time_node = nullptr; // from the importer
    motor::wire::inode_mtr_t _merger = nullptr;        // from the importer

    // store all the wire nodes for easier destruction.
    motor::vector< motor::wire::inode_mtr_t > _node_dump;

    // we need this flag for proper release.
    // if the async chain is done, this flag is tirggered.
    std::shared_ptr< std::atomic_bool > _async_done = std::make_shared< std::atomic_bool >( true );

  private: // shader variable slots

    motor_typedefs( motor::wire::input_slot< float_t >, float_is );
    motor_typedefs( motor::wire::input_slot< motor::math::vec3f_t >, vec3_is );

    motor_typedefs( motor::wire::output_slot< float_t >, float_os );
    motor_typedefs( motor::wire::output_slot< motor::math::vec3f_t >, vec3_os );

    float_os_mtr_t _shininess = motor::shared( float_os_t( 80.0f ) );
    float_os_mtr_t _specular_strength = motor::shared( float_os_t( 0.3f ) );
    float_os_mtr_t _light_intensity = motor::shared( float_os_t( 2.0f ) );
    float_os_mtr_t _bias = motor::shared( float_os_t( 0.001f ) );

    vec3_os_mtr_t _hemi_top_color =
        motor::shared( vec3_os_t( motor::math::vec3f_t( 0.24f, 0.28f, 0.36f ) ) );

  public:

    explicit my_app( options const & opts ) : _options( opts ) {}

    motor::gfx::generic_camera_ptr_t view_camera( void_t ) noexcept
    {
        return _capture_view ? &_capture_camera : _cameras[ _cam_id ].second;
    }

    virtual void_t on_init( void_t ) noexcept
    {
        _db = motor::shared(
            motor::io::database( motor::io::path_t( MOTOR_SHADOW_ASSETS ), ".", "shadow29" ) );

        {
            _time = motor::shared( os_float_t( 0.0f ) );
            _scale_os = motor::shared( os_trafo_t() );
        }

        {
            motor::application::window_info_t wi;
            wi.x = 40;
            wi.y = 40;
            wi.w = 1280;
            wi.h = 720;
            wi.window_name = "motor | Sponza shadow mapping";
            _options.capture.apply( wi );
            _width = uint_t( wi.w ); _height = uint_t( wi.h );
            wi.gen = _options.d3d ? motor::application::graphics_generation::gen4_d3d11 :
                motor::application::graphics_generation::gen4_gl4;

            this_t::send_window_message( this_t::create_window( wi ),
                [ & ]( motor::application::app::window_view & wnd )
            {
                wnd.send_message( motor::application::show_message( { true } ) );
                wnd.send_message( motor::application::cursor_message_t( { true } ) );
                wnd.send_message( motor::application::vsync_message_t( { true } ) );
            } );
        }

        // the main state sets comes for the ppp. This is only doing a
        // difference state change for rendering this scene.
        // the ppp provides
        // 1. render states for the depth pass
        // 2. render states for the color/light pass
        {
            motor::graphics::state_object_t so =
                motor::graphics::state_object_t( "final_render_states" );

            {
                motor::graphics::render_state_sets_t rss;
                // One opaque camera pass owns depth, rather than comparing independently
                // rounded vertex positions against a separate depth-only shader.
                rss.depth_s.do_change = true;
                rss.depth_s.ss.do_activate = true;
                rss.depth_s.ss.do_depth_write = true;
                rss.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal;
                rss.clear_s.do_change = true;
                rss.clear_s.ss.do_activate = true;
                rss.clear_s.ss.do_color_clear = false;
                rss.clear_s.ss.do_depth_clear = true;
                rss.polygon_s.do_change = true;
                rss.polygon_s.ss.do_activate = true;
                rss.polygon_s.ss.fm = motor::graphics::fill_mode::fill;
                rss.polygon_s.ss.ff = motor::graphics::front_face::counter_clock_wise;
                rss.polygon_s.ss.cm = motor::graphics::cull_mode::back;

                so.add_render_state_set( rss );
            }

            _final_so = motor::shared( motor::graphics::state_object_t( std::move( so ) ) );
        }

        {
            motor::graphics::state_object_t so =
                motor::graphics::state_object_t( "light_render_states" );

            {
                motor::graphics::render_state_sets_t rss;
                rss.depth_s.do_change = true;
                rss.depth_s.ss.do_activate = true;
                rss.depth_s.ss.do_depth_write = true;
                rss.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal;

                rss.blend_s.do_change = true;
                rss.blend_s.ss.do_activate = false;
                rss.blend_s.ss.blend_func = motor::graphics::blend_function::add;
                rss.blend_s.ss.dst_blend_factor = motor::graphics::blend_factor::one;
                rss.blend_s.ss.src_blend_factor = motor::graphics::blend_factor::src_alpha;



                so.add_render_state_set( rss );
            }

            _light_so = motor::shared( motor::graphics::state_object_t( std::move( so ) ) );
        }

        {
            motor::graphics::state_object_t so =
                motor::graphics::state_object_t( "shadow_render_states" );

            {
                motor::graphics::render_state_sets_t rss;
                rss.depth_s.do_change = true;
                rss.depth_s.ss.do_activate = true;
                rss.depth_s.ss.do_depth_write = true;
                rss.polygon_s.do_change = true;
                rss.polygon_s.ss.do_activate = true;
                rss.polygon_s.ss.fm = motor::graphics::fill_mode::fill;
                rss.polygon_s.ss.ff = motor::graphics::front_face::counter_clock_wise;
                rss.polygon_s.ss.cm = motor::graphics::cull_mode::back;
                rss.depth_s.ss.compare_funk = motor::graphics::depth_compare::less;

                rss.clear_s.do_change = true;
                rss.clear_s.ss.clear_color = motor::math::vec4f_t( 0.0f, 0.0f, 0.0f, 1.0f );
                rss.clear_s.ss.do_activate = true;
                rss.clear_s.ss.do_color_clear = false;
                rss.clear_s.ss.do_depth_clear = true;
                rss.view_s.do_change = true;
                rss.view_s.ss.do_activate = true;
                rss.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, 2048, 2048 );
                so.add_render_state_set( rss );
            }

            _shadow_depth_so = motor::shared( motor::graphics::state_object_t( std::move( so ) ) );
        }

        {
            _pp_pipe = motor::shared( motor::gfx::hdr_postprocess_pipeline_t( _width, _height ) );
            _pp_pipe->init();
        }

        {
            motor::graphics::render_state_sets_t rss;
            rss.view_s.do_change = true;
            rss.view_s.ss.do_activate = true;
            rss.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, _width, _height );
            _present_state.add_render_state_set( rss );
        }

        // init scene tree
        // - import the gltf file
        // - connect animation nodes from the import with
        // app nodes/slots for easy animation update.
        // - collect the cameras
        {
            motor::scene::logic_group_t root;
            root.add_component( motor::shared( motor::scene::name_component_t( "intro scene" ) ) );

            // add imported scene
            {
                motor::scene::node_mtr_t imported_node = nullptr;

                auto group = motor::shared( motor::scene::logic_group_t() );

                // make importer ready
                {
                    motor::format::module_registry_mtr_t mod_reg =
                        motor::format::global::register_default_modules(
                            motor::shared( motor::format::module_registry_t(), "mod registry" ) );

                    // import the gltf asset.
                    {
                        motor::property::property_sheet_t ps;
                        ps.add_property< motor::string_t >(
                            "base_name", motor::property::generic_property< motor::string_t >(
                                             "shadow29_sponza" ) );

                        motor::io::database_mtr_t scene_db = motor::shared( motor::io::database_t(
                            motor::io::path_t( MOTOR_SPONZA_ASSETS ), ".", "shadow29_sponza" ) );
                        auto item = mod_reg->import_from(
                            motor::io::location_t( "Sponza.Sponza.gltf" ), scene_db,
                            motor::shared( std::move( ps ) ) );

                        auto * ret_item = item.get();

                        if( auto * scene_item =
                                dynamic_cast< motor::format::scene_item_ptr_t >( ret_item );
                            scene_item != nullptr )
                        {
                            imported_node = motor::move( scene_item->root );

                            motor::wire::release( motor::move( _time_node ) );
                            motor::wire::release( motor::move( _merger ) );

                            // Sponza is static; only the application's camera is animated.
                            // Leave the importer's optional animation nodes with scene_item.

                            // camera sequence
                            {
                                for( auto & csi : _cs ) motor::release( motor::move( csi.cam ) );

                                _cs.clear();

                                for( auto & i : scene_item->camera_sequence )
                                {
                                    this_t::camera_sequence_item csi;
                                    csi.cam = motor::move( i.cam );
                                    csi.start = i.start;
                                    csi.end = i.end;
                                    _cs.emplace_back( csi );
                                }
                            }
                        }
                        else
                        {
                            motor::log::global_t::critical( "Failed to load gltf file." );
                            std::exit( 1 );
                        }

                        ret_item->release();
                        motor::release( motor::move( ret_item ) );
                        motor::release( motor::move( scene_db ) );
                    }

                    motor::release( motor::move( mod_reg ) );
                }

                // Keep Sponza's imported 0.008 root scale and all child transforms.
                group->add_child( motor::move( imported_node ) );
                root.add_child( motor::move( group ) );
            }

            motor::release( motor::move( _root ) );
            _root = motor::shared( std::move( root ) );
            auto sun = motor::shared( motor::gfx::directional_light_t( shadow_sample::sun_direction() ) );
            sun->set_shadow_casting( true );
            _root->add_component( motor::shared( motor::scene::light_component_t( motor::move( sun ) ) ) );
        }

        // Static scenes need neither an animation chain nor a completion wait.
        if( _time_node && _merger )
        {
            this_t::release_node_dump();

            auto t = motor::shared( motor::wire::funk_node_t(
                [ this ]( motor::wire::funk_node_ptr_t ) { _async_done->store( true, std::memory_order_release ); } ) );

            _node_dump.emplace_back( motor::share( t ) );
            _merger->then( motor::move( t ) );
        }
        // motor::release( motor::move( _selected_node ) );

        // reload cameras
        {
            this_t::release_cameras();

            motor::scene::node_t::traverser( _root ).apply( &_cc );
            auto cams = _cc.get_cameras();

            if( cams.size() > 0 )
            {
                size_t i = 0;
                _cameras.resize( cams.size() );
                for( auto & c : cams )
                {
                    _cameras[ i ].first = c.first;
                    _cameras[ i++ ].second = motor::share( c.second );
                }
                _cam_id = 0;
            }
            else
            {
                motor::log::global_t::status( "[shadow29] Using the Sponza courtyard camera." );
            }
        }

        // Create the fitted sun camera and a depth-only shadow framebuffer.
        {
            motor::gfx::generic_camera_t gcam;
            _sun_cam = motor::shared( std::move( gcam ) );

            _cameras.emplace_back( std::make_pair( "sun cam", motor::share( _sun_cam ) ) );
            _cam_id = 0;

            auto fb = motor::graphics::framebuffer_object_t(
                "29_camera_fit_shadow_mapping.shadow_depth_framebuffer" );
            fb.set_target( motor::graphics::depth_stencil_target_type::depth32 )
                .resize( 2048, 2048 );

            motor::release( motor::move( _shadow_depth_fb ) );
            _shadow_depth_fb = motor::shared( std::move( fb ) );
        }

        // manager
        {
            _own_mmgr = motor::shared( motor::gfx::msl_manager_t( motor::share( _db ) ) );
            _own_mmgr->add( "color_pass",
                motor::io::location_t( "29_camera_fit_shadow_mapping.shaders.color_pass.msl" ) );
            _own_mmgr->add( "light_pass",
                motor::io::location_t( "29_camera_fit_shadow_mapping.shaders.light_pass.msl" ) );
            _own_mmgr->add( "depth_pass",
                motor::io::location_t( "29_camera_fit_shadow_mapping.shaders.depth_pass.msl" ) );
            _own_mmgr->add( "normal_vis_pass",
                motor::io::location_t(
                    "29_camera_fit_shadow_mapping.shaders.normal_vis_pass.msl" ) );
            _own_mmgr->add( "shadow_depth_pass",
                motor::io::location_t(
                    "29_camera_fit_shadow_mapping.shaders.shadow_depth_pass.msl" ) );
            _own_mmgr->add( "shadow_light_pass",
                motor::io::location_t(
                    "29_camera_fit_shadow_mapping.shaders.shadow_light_pass.msl" ) );

            _reg0 = _own_mmgr->register_funk(
                [ this ]( motor::string_in_t msl_name, motor::graphics::msl_object_mtr_t msl )
            {
                if( msl_name == "color_pass" )
                {
                    motor::scene::add_msl_to_set_visitor_t v(
                        this_file::to_id( this_file::msl_id::color_pass_id ), motor::share( msl ) );
                    motor::scene::node_t::traverser( _root ).apply( &v );
                }
                else if( msl_name == "normal_vis_pass" )
                {
                    motor::scene::add_msl_to_set_visitor_t v(
                        this_file::to_id( this_file::msl_id::normal_pass_id ),
                        motor::share( msl ) );
                    motor::scene::node_t::traverser( _root ).apply( &v );
                }
                else if( msl_name == "light_pass" )
                {
                    {
                        motor::scene::add_msl_to_set_visitor_t v(
                            this_file::to_id( this_file::msl_id::light_pass_id ),
                            motor::share( msl ) );
                        motor::scene::node_t::traverser( _root ).apply( &v );
                    }

                    {
                        motor::scene::connect_msl_slot_visitor_t v(
                            this_file::to_id( this_file::msl_id::light_pass_id ),
                            [ & ]( motor::wire::inputs_ref_t inputs )
                        {
                            {
                                auto * input = inputs.borrow_or_add(
                                    "shininess", motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _shininess ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add( "specular_strength",
                                    motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _specular_strength ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add( "light_intensity",
                                    motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _light_intensity ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add(
                                    "bias", motor::shared( this_t::float_is_t( 0.02f ) ) );
                                if( input ) input->connect( motor::share( _bias ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add(
                                    "hemi_top_color", motor::shared( this_t::vec3_is_t() ) );
                                if( input ) input->connect( motor::share( _hemi_top_color ) );
                            }
                        } );
                        motor::scene::node_t::traverser( _root ).apply( &v );
                    }
                }
                else if( msl_name == "shadow_light_pass" )
                {
                    {
                        motor::scene::add_msl_to_set_visitor_t v(
                            this_file::to_id( this_file::msl_id::shadow_light_id ),
                            motor::share( msl ) );
                        motor::scene::node_t::traverser( _root ).apply( &v );
                    }

                    {
                        motor::scene::connect_msl_slot_visitor_t v(
                            this_file::to_id( this_file::msl_id::shadow_light_id ),
                            [ & ]( motor::wire::inputs_ref_t inputs )
                        {
                            {
                                auto * input = inputs.borrow_or_add(
                                    "shininess", motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _shininess ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add( "specular_strength",
                                    motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _specular_strength ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add( "light_intensity",
                                    motor::shared( this_t::float_is_t( 0.0f ) ) );
                                if( input ) input->connect( motor::share( _light_intensity ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add(
                                    "bias", motor::shared( this_t::float_is_t( 0.02f ) ) );
                                if( input ) input->connect( motor::share( _bias ) );
                            }

                            {
                                auto * input = inputs.borrow_or_add(
                                    "hemi_top_color", motor::shared( this_t::vec3_is_t() ) );
                                if( input ) input->connect( motor::share( _hemi_top_color ) );
                            }
                        } );
                        motor::scene::node_t::traverser( _root ).apply( &v );
                    }
                }
                else if( msl_name == "shadow_depth_pass" )
                {
                    motor::scene::add_msl_to_set_visitor_t v(
                        this_file::to_id( this_file::msl_id::shadow_depth_id ),
                        motor::share( msl ) );
                    motor::scene::node_t::traverser( _root ).apply( &v );
                }
                else if( msl_name == "depth_pass" )
                {
                    motor::scene::add_msl_to_set_visitor_t v(
                        this_file::to_id( this_file::msl_id::depth_pass_id ), motor::share( msl ) );
                    motor::scene::node_t::traverser( _root ).apply( &v );
                }
            } );
        }

        {
            motor::gfx::primitive_render_3d_t prim_render;
            prim_render.init( "my_debug_render" );
            _prim_render = motor::shared( std::move( prim_render ) );
        }
    }

    virtual void_t on_event( window_id_t const wid,
        motor::application::window_message_listener::state_vector_cref_t sv ) noexcept
    {
        if( sv.create_changed )
        {
            motor::log::global_t::status( "[my_app] : window created" );
        }
        if( sv.close_changed )
        {
            motor::log::global_t::status( "[my_app] : window closed" );
            this->close();
        }
        if( sv.resize_changed && sv.resize_msg.resize && sv.resize_msg.w > 0 && sv.resize_msg.h > 0 )
        {
            _width = uint_t( sv.resize_msg.w ); _height = uint_t( sv.resize_msg.h );
            _viewport_dirty = true;
        }
    }

    //******************************************************************************************************
    virtual void_t on_update( motor::application::app::update_data_in_t ud ) noexcept
    {
        _own_mmgr->on_update();

        // The imported animation loops over fifteen seconds.
        {
            if( !_pause ) _elapsed += ud.sec_dt;
            if( _animate_sun ) _sun_seconds += ud.sec_dt;
            _time->set_and_exchange( std::fmod( _elapsed, 15.0f ) );
        }

        // Finish animation jobs before traversing their inputs and world bounds.
        if( _time_node && _merger )
        {
            _async_done->store( false, std::memory_order_relaxed );
            motor::concurrent::global_t::schedule(
                _time_node->get_task(), motor::concurrent::schedule_type::pool );
        }

// sync of pool tasks need to be done by the user itself.
// this is just an example. Do syncronization by any means
// you find necessarry.
        {
            while( !_async_done->load( std::memory_order_acquire ) ) std::this_thread::yield();
        }

    }

    //******************************************************************************************************
    virtual void_t on_graphics( motor::application::app::graphics_data_in_t gd ) noexcept
    {
        // absolutely required. this visitor bakes local
        // transformations, i.e. if animations occure or if
        // the local trafo is just commited to the usable
        // transformation by the trafo visitor
        // must be called in on graphics at the moment.
        {
            motor::scene::variable_update_visitor_t v;
            motor::scene::node_t::traverser( _root ).apply( &v );
        }

        {
            motor::scene::trafo_visitor_t transforms;
            motor::scene::node_t::traverser( _root ).apply( &transforms );
            _world_bounds = transforms.get_world_box();
        }

        shadow_sample::sponza_camera( _capture_camera, _world_bounds,
            float_t( _width ) / float_t( _height ), _elapsed );
        auto * cam = view_camera();

// test

        // sun cam creation
        // here we need the world box which is used to create the othographic box
        // so that the camera can see all the geometry/objects in the scene
        // 1. create loot_at using the light direction
        // 2. transform the world aabb into view space world aabb-> view obb-> view aabb
        // 3. use view aabb for width, height, near, far computation
        // 4. create orthographic projection
        // 5. fix cam position in view space using the view aabb
        // 6. transform new cam position back to world space and assign to camera.
        {
            motor::gfx::directional_light_mtr_t lg = nullptr;

            // find sun light direction
            // in this case, just take the first directional we find and
            // use that as the sun light.
            {
                motor::scene::light_collector_visitor_t vis;
                motor::scene::node_t::traverser( _root ).apply( &vis );
                vis.for_each_light( [ & ]( size_t const i, motor::gfx::light_mtr_t light )
                {
                    if( light->is_shadow_casting() &&
                        light->get_light_type() == motor::gfx::light_type::directional )
                    {
                        lg = static_cast< motor::gfx::directional_light_mtr_t >( light );
                        return false;
                    }
                    return true;
                } );
            }

            if( lg != nullptr )
            {
                lg->set_direction( shadow_sample::sun_direction( _sun_seconds ) );
                shadow_sample::fit_light( *_sun_cam, _world_bounds, lg->get_transformed_direction() );
            }
        }

        // use the debug_graphics_visitor_t and render the scene
        {
            if( _draw_debug )
            {
                motor::scene::debug_graphics_visitor_t::draw_context dc = {
                    cam, _prim_render, _draw_depth, _draw_leafs };

                motor::scene::debug_graphics_visitor_t v( dc );
                motor::scene::node_t::traverser( _root ).apply( &v );

                _max_depth = v.get_max_depth();
            }

            // render sun_cam frustum
            if( _draw_sun_ortho )
            {
                std::array< motor::math::vec3f_t, 8 > points;
                if( _sun_cam->get_frustum().points( points ) )
                {
                    _prim_render->draw_wire_cube(
                        points, motor::math::vec4f_t( 0.0f, 1.0f, 0.0f, 1.0f ) );
                }
            }

            _prim_render->set_view_proj( cam->get_view_matrix(), cam->get_proj_matrix() );
            _prim_render->prepare_for_rendering();
        }
    }

    virtual void_t on_first_frame( this_t::window_id_t const wid,
        motor::graphics::gen4::frontend_ptr_t fe,
        motor::application::app::render_data_in_t ) noexcept
    {
        _pp_pipe->init_render( fe );
        _own_mmgr->on_render_init( fe );
        fe->configure< motor::graphics::state_object_t >( &_present_state );
        fe->configure< motor::graphics::state_object_t >( _final_so );
        fe->configure< motor::graphics::state_object_t >( _light_so );
        fe->configure< motor::graphics::state_object_t >( _shadow_depth_so );
        fe->configure< motor::graphics::framebuffer_object_t >( _shadow_depth_fb );

        _prim_render->configure( fe );
    }

    virtual void_t on_last_frame( this_t::window_id_t const wid,
        motor::graphics::gen4::frontend_ptr_t fe,
        motor::application::app::render_data_in_t ) noexcept
    {
        _pp_pipe->release_render( fe );
        _own_mmgr->on_render_release( fe );
        fe->release< motor::graphics::state_object_t >( &_present_state );
        fe->release< motor::graphics::state_object_t >( _final_so );
        fe->release< motor::graphics::state_object_t >( _light_so );
        fe->release< motor::graphics::state_object_t >( _shadow_depth_so );
        fe->release< motor::graphics::framebuffer_object_t >( _shadow_depth_fb );

        _prim_render->release( fe );
    }

    virtual void_t on_render( this_t::window_id_t const wid,
        motor::graphics::gen4::frontend_ptr_t fe,
        motor::application::app::render_data_in_t ) noexcept
    {
        _own_mmgr->on_render( fe );

        if( _viewport_dirty )
        {
            _present_state.access_render_state( 0, [this]( motor::graphics::render_state_sets_ref_t ss )
            {
                ss.view_s.ss.vp = motor::math::vec4ui_t( 0, 0, _width, _height );
                return true;
            } );
            fe->configure< motor::graphics::state_object_t >( &_present_state );
            _viewport_dirty = false;
        }

        // make shadow pass for sun light
        if( _sun_cam )
        {
            fe->use( _shadow_depth_fb );
            fe->push( _shadow_depth_so );
            motor::gfx::generic_camera_mtr_t cam = _sun_cam;
            motor::scene::render_visitor_t vis(
                    this_file::to_id( this_file::msl_id::shadow_depth_id ), fe, cam );
            motor::scene::node_t::traverser( _root ).apply( &vis );
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
            fe->unuse( motor::graphics::gen4::backend::unuse_type::framebuffer );
        }

        // render into final framebuffer
        {
            // activate fb 0
            fe->use( _pp_pipe->borrow_hdr_fb( 0 ) );

            // The opaque color/light pass clears and writes its own depth.
            {
                fe->push( _pp_pipe->borrow_hdr_states() );
                this_t::on_render_scene( fe );
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
            }
            fe->unuse( motor::graphics::gen4::backend::unuse_type::framebuffer );
        }

        // present framebuffer
        {
            fe->push( &_present_state );
            _pp_pipe->render( fe, _show_temp_rt );
        }

        _prim_render->prepare_for_rendering( fe );
        _prim_render->render( fe );
        fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
    }

    //************************************************************************************
    virtual void_t on_render_depth_pass( motor::graphics::gen4::frontend_ptr_t fe ) noexcept
    {
        if( _cam_id != size_t( -1 ) )
        {
            fe->push( _final_so );
            {
                motor::gfx::generic_camera_ptr_t cam = view_camera();
                motor::scene::render_visitor_t vis(
                    this_file::to_id( this_file::msl_id::depth_pass_id ), fe, cam );

                motor::scene::node_t::traverser( _root ).apply( &vis );
            }

            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }
    }

    //************************************************************************************
    virtual void_t on_render_scene( motor::graphics::gen4::frontend_ptr_t fe ) noexcept
    {
        size_t render_id = size_t( -1 );

        if( _cam_id != size_t( -1 ) )
        {
            fe->push( _final_so );

            if( _render_normals )
            {
                motor::scene::render_visitor_t rnd_vis(
                    this_file::to_id( this_file::msl_id::normal_pass_id ), ++render_id, fe,
                    view_camera() );
                motor::scene::node_t::traverser( _root ).apply( &rnd_vis );
            }
            else if( _render_default )
            {
                motor::scene::render_visitor_t rnd_vis(
                    this_file::to_id( this_file::msl_id::color_pass_id ), ++render_id, fe,
                    view_camera() );
                motor::scene::node_t::traverser( _root ).apply( &rnd_vis );
            }

            else if( _render_light )
            {
                fe->push( _light_so );
                if( _sun_cam )
                {

                    motor::gfx::directional_light_t dl( _sun_cam->get_direction().negated() );
                    motor::gfx::single_shadow_data_t sd = { _sun_cam->get_proj_matrix(),
                        _sun_cam->get_view_matrix(),
                        "29_camera_fit_shadow_mapping.shadow_depth_framebuffer.depth" };

                    motor::gfx::generic_camera_ptr_t cam = view_camera();

                    motor::scene::light_pass_render_visitor_t vis(
                        this_file::to_id( this_file::msl_id::shadow_light_id ), ++render_id, fe,
                        cam, &dl, &sd );

                    motor::scene::node_t::traverser( _root ).apply( &vis );
                }
                fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
            }

            fe->pop( motor::graphics::gen4::backend::pop_type::render_state );
        }
    }

    //************************************************************************************
    virtual void_t on_frame_done( void_t ) noexcept
    {
        _own_mmgr->on_frame_done();
    }

    //******************************************************************************************************
    bool_t on_tool(
        this_t::window_id_t const wid, motor::application::app::tool_data_ref_t td ) noexcept
    {
        if( _options.demo ) return false;
        ImGui::Checkbox( "Courtyard camera", &_capture_view );
        ImGui::Checkbox( "Pause camera", &_pause );
        ImGui::Checkbox( "Animate sun", &_animate_sun );
        // SECTION: cameras
        {
            auto const & cams = _cameras;

            if( cams.size() > 0 )
            {
                size_t i = 0;
                static ImGuiComboFlags flags = 0;
                motor::vector< char const * > items( cams.size() );
                for( auto const & e : cams )
                {
                    items[ i++ ] = e.first.c_str();
                }

                motor::string_t combo_name = "Scene Camera#";

                int item_selected_idx = _cam_id != size_t( -1 ) ? int_t( _cam_id ) : 0;
                const char * combo_preview_value = items[ item_selected_idx ];
                if( ImGui::BeginCombo( combo_name.c_str(), combo_preview_value, flags ) )
                {
                    for( int n = 0; n < int( items.size() ); n++ )
                    {
                        const bool is_selected = ( item_selected_idx == n );
                        if( ImGui::Selectable( items[ n ], is_selected ) ) item_selected_idx = n;

                        // Set the initial focus when opening the combo (scrolling +
                        // keyboard navigation focus)
                        if( is_selected ) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();

                    _cam_id = size_t( item_selected_idx );
                }
                ImGui::Separator();
                auto * cam_ptr = _cameras[ _cam_id ].second;
                if( cam_ptr->is_orthographic() )
                    ImGui::Text( "Type : orthographic " );
                else if( cam_ptr->is_perspective() )
                    ImGui::Text( "Type : perspecitve " );

                auto const nf = cam_ptr->get_near_far();
                ImGui::Text( "[Near; Far] : [ %f;%f]", nf.x(), nf.y() );
            }
        }

        if( ImGui::Begin( "Rendering" ) )
        {

            {
                ImGui::Checkbox( "Render Debug", &_draw_debug );
                ImGui::Checkbox( "Render Leafs", &_draw_leafs );
                {
                    int_t v = int_t( _draw_depth );
                    ImGui::SliderInt( "Render Level", &v, 0, int_t( _max_depth ) );
                    _draw_depth = size_t( v );
                }
                ImGui::Separator();
            }

            if( ImGui::Checkbox( "Render Light", &_render_light ) && _render_light )
            {
                _render_normals = false;
                _render_default = false;
            }
            ImGui::Checkbox( "Draw Sun Light Ortho", &_draw_sun_ortho );

            if( ImGui::Checkbox( "Render Normals", &_render_normals ) )
            {
                if( _render_normals )
                {
                    _render_default = false;
                    _render_light = false;
                }
            }

            if( ImGui::Checkbox( "Render Default", &_render_default ) )
            {
                if( _render_default )
                {
                    _render_normals = false;
                    _render_light = false;
                }
            }

            {
                static char const * const labels[] = { "Shadow depth",
                    "HDR color 0", "HDR color 1", "HDR color 2",
                    "HDR color 3", "HDR depth" };
                static char const * const textures[] = {
                    "29_camera_fit_shadow_mapping.shadow_depth_framebuffer.depth",
                    "gfx.postprocess.hdr.framebuffer.0.0", "gfx.postprocess.hdr.framebuffer.0.1",
                    "gfx.postprocess.hdr.framebuffer.0.2", "gfx.postprocess.hdr.framebuffer.0.3",
                    "gfx.postprocess.hdr.framebuffer.0.depth" };
                static_assert( IM_ARRAYSIZE( labels ) == IM_ARRAYSIZE( textures ) );

                ImGui::Separator();

                ImGui::Checkbox( "Temp Render Target", &_show_temp_rt );

                if( ImGui::ListBox(
                        "Render target", &_temp_rt_index, labels, IM_ARRAYSIZE( labels ), 5 ) )
                {
                }

                if( _show_temp_rt )
                {
                    _pp_pipe->set_map_to_screen_texture_temp( textures[ _temp_rt_index ] );
                }
            }
        }
        ImGui::End();

        if( ImGui::Begin( "Post Process" ) )
        {
            bool_t has_changed = false;
            for( auto ps : _pp_pipe->property_sheets() )
            {
                has_changed |= motor::tool::imgui_property::handle( ps.first, *ps.second );
            }
            if( has_changed ) _pp_pipe->update_properies();

            {
                ImGui::Checkbox( "Show temp render target ##scene_manager", &_show_temp_rt );
            }
        }
        ImGui::End();

        if( ImGui::Begin( "Light Variables" ) )
        {
            {
                float_t v = _shininess->get_value();
                if( ImGui::SliderFloat( "Shininess", &v, 0.0f, 100.0f ) )
                {
                    _shininess->set_and_exchange( v );
                }
            }

            {
                float_t v = _specular_strength->get_value();
                if( ImGui::SliderFloat( "specular_strength", &v, 0.0f, 100.0f ) )
                {
                    _specular_strength->set_and_exchange( v );
                }
            }

            {
                float_t v = _light_intensity->get_value();
                if( ImGui::SliderFloat( "light_intensity", &v, 0.1f, 10.0f ) )
                {
                    _light_intensity->set_and_exchange( v );
                }
            }

            {

                float_t v[ 3 ] = { _hemi_top_color->get_value().x(),
                    _hemi_top_color->get_value().y(), _hemi_top_color->get_value().z() };

                if( ImGui::SliderFloat3( "upper hemnisphere color", v, 0.1f, 10.0f ) )
                {
                    _hemi_top_color->set_and_exchange( motor::math::vec3f_t( v ) );
                }
            }

            {
                float_t v = _bias->get_value();
                if( ImGui::SliderFloat( "bias", &v, 0.0001f, 0.02f, "%.5f" ) )
                {
                    _bias->set_and_exchange( v );
                }
            }
        }
        ImGui::End();
        return true;
    }

    //******************************************************************************************************
    void_t on_shutdown( void_t ) noexcept
    {
        while( !_async_done->load( std::memory_order_acquire ) ) std::this_thread::yield();
        release_all_objects();
    }

  private:

    void_t release_cameras( void_t ) noexcept
    {
        _cc.release();
        for( auto & cam : _cameras )
        {
            motor::release( motor::move( cam.second ) );
        }
        _cameras.clear();

        for( auto & csi : _cs ) motor::release( motor::move( csi.cam ) );
        _cs.clear();
    }

    void_t release_node_dump( void_t ) noexcept
    {
        for( auto * ptr : _node_dump )
        {
            ptr->disconnect();
            motor::release( motor::move( ptr ) );
        }
        _node_dump.clear();
    }

    void_t release_all_objects( void_t ) noexcept
    {
        motor::release( motor::move( _pp_pipe ) );
        motor::release( motor::move( _db ) );

        motor::wire::release( motor::move( _time ) );
        motor::wire::release( motor::move( _scale_os ) );
        motor::wire::release( motor::move( _time_node ) );
        motor::wire::release( motor::move( _merger ) );

        motor::release( motor::move( _final_so ) );
        motor::release( motor::move( _light_so ) );

        motor::release( motor::move( _root ) );

        this_t::release_cameras();
        this_t::release_node_dump();

        motor::release( motor::move( _own_mmgr ) );

        motor::release( motor::move( _shininess ) );
        motor::release( motor::move( _specular_strength ) );
        motor::release( motor::move( _light_intensity ) );
        motor::release( motor::move( _hemi_top_color ) );
        motor::release( motor::move( _bias ) );

        motor::release( motor::move( _shadow_depth_fb ) );
        motor::release( motor::move( _shadow_depth_so ) );
        motor::release( motor::move( _sun_cam ) );

        motor::release( motor::move( _prim_render ) );
    }
};
} // namespace this_file

int main( int argc, char ** argv )
{
    using namespace motor::core::types;
    this_file::options opts;
    bool_t help = false;
    for( int i = 1; i < argc; ++i )
    {
        motor::string_t const arg( argv[i] );
        if( arg == "--gl-only" ) opts.d3d = false;
        else if( arg == "--d3d-only" ) opts.d3d = true;
        else if( arg == "--demo" ) opts.demo = true;
        else if( arg == "--cpu-only" ) opts.cpu_only = true;
        else if( arg == "--help" ) help = true;
        else if( arg == "--youtube-short" && i + 1 < argc && opts.capture.parse( argv[i+1] ) ) ++i;
        else
        {
            motor::log::global_t::error( "Unknown or incomplete option: " + arg );
            motor::log::global_t::deinit();
            return 2;
        }
    }
    if( help )
    {
        motor::log::global_t::status( "29_camera_fit_shadow_mapping [--gl-only|--d3d-only] [--demo] [--youtube-short square|vertical] [--cpu-only]" );
        motor::log::global_t::deinit();
        return 0;
    }
#ifndef _WIN32
    if( opts.d3d )
    {
        motor::log::global_t::error( "d3d11 requires Windows." );
        motor::log::global_t::deinit();
        return 2;
    }
#endif
    if( opts.cpu_only )
    {
        bool_t passed = true;
        using vec3_t = motor::math::vec3f_t;
        motor::collide::aabb3f_t const boxes[] = {
            { vec3_t(4,3,2), vec3_t(-5,-1,-3) },
            { vec3_t(103,24,-50), vec3_t(91,21,-59) },
            { vec3_t(1,0,1), vec3_t(-1,0,-1) } };
        vec3_t const directions[] = { vec3_t(1,-2,3), vec3_t(-2,-1,-3), vec3_t(0,-1,0) };
        for( auto const & box : boxes )
        {
            motor::gfx::generic_camera_t camera;
            for( auto const & direction : directions )
            {
                shadow_sample::fit_light( camera, box, direction );
                passed &= shadow_sample::encloses( camera, box );
            }
            for( float_t const aspect : { 16.0f/9.0f, 1.0f, 9.0f/16.0f } )
            {
                shadow_sample::fit_view( camera, box, aspect, vec3_t(0.25f,0.18f,1.0f) );
                passed &= shadow_sample::encloses( camera, box );
            }
        }
        motor::log::global_t::test( "Light and capture cameras enclose all AABB corners", passed );
        auto const assets = std::filesystem::path( MOTOR_SPONZA_ASSETS ) / "Sponza";
        bool_t const assets_ok = std::filesystem::exists( assets / "Sponza.gltf" ) &&
            std::filesystem::exists( assets / "Sponza.bin" );
        motor::log::global_t::test( "Bundled Sponza scene", assets_ok );
        if( assets_ok ) passed &= shadow_sample::check_assets();
        motor::concurrent::global_t::deinit();
        motor::io::global_t::deinit();
        motor::log::global_t::deinit();
        return passed && assets_ok ? 0 : 1;
    }

    motor::application::carrier_mtr_t carrier =
        motor::platform::global_t::create_carrier( motor::shared( this_file::my_app( opts ) ) );

    auto const ret = carrier->exec();

    motor::memory::release_ptr( carrier );

    motor::io::global::deinit();
    motor::concurrent::global::deinit();
    motor::log::global::deinit();
    motor::memory::global::dump_to_std();

    return ret;
}
