#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/scene/node/logic_group.h>
#include <motor/scene/node/logic_leaf.h>
#include <motor/scene/component/trafo3d_component.h>
#include <motor/scene/component/graphics/msl_set_component.hpp>
#include <motor/scene/visitor/variable_update_visitor.h>
#include <motor/scene/visitor/trafo_visitor.h>
#include <motor/scene/visitor/graphics/render_visitor.h>
#include <motor/scene/visitor/graphics/light_pass_render_visitor.h>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/gfx/camera/generic_camera.h>
#include <motor/concurrent/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/std/vector>
#include <motor/std/string>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>

namespace sample
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;
    double_t ms( clock_t::time_point a, clock_t::time_point b ) noexcept
    { return std::chrono::duration<double_t, std::milli>( b - a ).count() ; }

    struct options
    {
        size_t objects = 1000, lights = 2, warmup = 120, frames = 300 ;
        motor::string_t backend = "gl" ;
        motor::string_t path = "scene" ;
        motor::string_t lighting = "multipass" ;
    } ;

    struct measurement
    {
        double_t frame = 0, sync = 0, trafo = 0, prepare[2] = {} ;
    } ;

    class benchmark_app : public motor::application::app
    {
        options _opts ;
        bool_t * _passed ;
        motor::graphics::geometry_object_t _geometry ;
        motor::graphics::state_object_t _state ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        motor::scene::logic_group_mtr_t _root = nullptr ;
        motor::gfx::generic_camera_t _camera ;
        motor::vector<motor::scene::trafo3d_component_mtr_t> _transforms ;
        struct direct_object
        {
            motor::math::vec3f_t position ;
            float_t scale ;
            size_t varset ;
            motor::graphics::data_variable<motor::math::mat4f_t> * world ;
        } ;
        motor::vector<direct_object> _direct_objects ;
        motor::vector<measurement> _samples ;
        window_id_t _windows[2] = {} ;
        bool_t _ready[2] = {}, _rendered[2] = {} ;
        size_t _warm = 0, _tick = 0 ;
        bool_t _closing = false ;
        measurement _current ;
        clock_t::time_point _started, _previous ;

        size_t window_count( void_t ) const noexcept { return _opts.backend == "dual" ? 2 : 1 ; }
        bool_t single_pass( void_t ) const noexcept { return _opts.lighting == "singlepass" ; }
        size_t pass_count( void_t ) const noexcept
        { return single_pass() ? 1 : std::max( size_t(1), _opts.lights ) ; }

        motor::math::vec3f_t light_direction( size_t const light ) const noexcept
        {
            float_t const a = float_t(light) * 6.28318530718f / float_t(_opts.lights) ;
            return motor::math::vec3f_t( std::cos(a), std::sin(a), 1.0f ) ;
        }
        bool_t all_ready( void_t ) const noexcept
        { return _ready[0] && ( window_count() == 1 || _ready[1] ) ; }

        void_t fail( char_cptr_t message ) noexcept
        {
            motor::log::global_t::error( message ) ;
            _closing = true ;
            close() ;
        }

        void_t make_geometry( void_t ) noexcept
        {
            struct vertex { motor::math::vec3f_t position, normal ; } ;
            auto vb = motor::graphics::vertex_buffer_t()
                .add_layout_element( motor::graphics::vertex_attribute::position,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .add_layout_element( motor::graphics::vertex_attribute::normal,
                    motor::graphics::type::tfloat, motor::graphics::type_struct::vec3 )
                .resize( 24 ).update<vertex>( []( vertex * v, size_t )
                {
                    float_t const p[8][3] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} } ;
                    uint_t const faces[6][4] = { {4,5,6,7},{1,0,3,2},{0,4,7,3},
                        {5,1,2,6},{3,7,6,2},{0,1,5,4} } ;
                    motor::math::vec3f_t const normals[6] = {
                        {0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0} } ;
                    for( size_t f = 0 ; f < 6 ; ++f )
                    {
                        for( size_t c = 0 ; c < 4 ; ++c )
                        {
                            auto const * xyz = p[faces[f][c]] ;
                            v[f*4+c] = { motor::math::vec3f_t( xyz[0], xyz[1], xyz[2] ), normals[f] } ;
                        }
                    }
                } ) ;
            auto ib = motor::graphics::index_buffer_t()
                .set_layout_element( motor::graphics::type::tuint )
                .resize( 36 ).update<uint_t>( []( uint_t * indices, size_t )
                {
                    uint_t const pattern[6] = {0,1,2,0,2,3} ;
                    for( uint_t f = 0 ; f < 6 ; ++f )
                        for( uint_t c = 0 ; c < 6 ; ++c ) indices[f*6+c] = f*4+pattern[c] ;
                } ) ;
            _geometry = motor::graphics::geometry_object_t( "benchmark_cube",
                motor::graphics::primitive_type::triangles, std::move( vb ), std::move( ib ) ) ;
        }

        void_t make_scene( void_t ) noexcept
        {
            motor::graphics::msl_object_t shader( "scene_benchmark", true ) ;
            motor::string_t code = R"(
                config scene_benchmark
                {
                    vertex_shader
                    {
                        in vec3_t pos : position ;
                        in vec3_t nrm : normal ;
                        out vec4_t pos : position ;
                        out vec3_t nrm : texcoord0 ;
                        mat4_t world : world ;
                        mat4_t view : view ;
                        mat4_t proj : projection ;
                        void main()
                        {
                            out.pos = proj * view * world * vec4_t( in.pos, 1.0 ) ;
                            out.nrm = normalize( world * vec4_t( in.nrm, 0.0 ) ).xyz ;
                        }
                    }
                    pixel_shader
                    {
                        in vec3_t nrm : texcoord0 ;
                        out vec4_t color : color ;
                        vec4_t base_color ;
            )" ;
            if( single_pass() )
            {
                // Generate ordinary named uniforms and unrolled expressions, not MSL arrays.
                for( size_t light = 0 ; light < _opts.lights ; ++light )
                {
                    code += "vec3_t light_dir_" + motor::to_string( light ) + " ;\n" ;
                }
                code += R"(
                        void main()
                        {
                            vec3_t normal = normalize( in.nrm ) ;
                            float_t diffuse = 0.0 ;
                )" ;
                for( size_t light = 0 ; light < _opts.lights ; ++light )
                {
                    code += "diffuse = diffuse + max( dot( normal, normalize( light_dir_" +
                        motor::to_string( light ) + " ) ), 0.0 ) ;\n" ;
                }
                code += "out.color = vec4_t( base_color.xyz * diffuse, 1.0 ) ; }\n" ;
            }
            else if( _opts.lights != 0 ) code += R"(
                        vec3_t light_dir : light_direction ;
                        void main()
                        {
                            float_t diffuse = max( dot( normalize( in.nrm ), normalize( light_dir ) ), 0.0 ) ;
                            out.color = vec4_t( base_color.xyz * diffuse, 1.0 ) ;
                        }
            )" ;
            else code += R"(
                        void main() { out.color = base_color ; }
            )" ;
            code += " } } " ;
            shader.add( motor::graphics::msl_api_type::msl_4_0, code ) ;
            size_t const geo = shader.link_geometry( "benchmark_cube" ) ;
            _shader = motor::shared( std::move( shader ) ) ;
            bool_t const direct = _opts.path == "direct" ;
            if( !direct )
            {
                _root = motor::shared( motor::scene::logic_group_t() ) ;
                _transforms.reserve( _opts.objects ) ;
            }
            else _direct_objects.reserve( _opts.objects ) ;
            _camera.make_orthographic( 24.0f, 13.5f, 1.0f, 100.0f ) ;
            _camera.look_at( motor::math::vec3f_t(0,0,30), motor::math::vec3f_t(0,1,0), motor::math::vec3f_t(0,0,0) ) ;
            size_t const columns = size_t( std::ceil( std::sqrt( double_t( _opts.objects ) * 16.0 / 9.0 ) ) ) ;
            size_t const rows = ( _opts.objects + columns - 1 ) / columns ;
            float_t const spacing = 22.0f / float_t( columns ) ;
            for( size_t i = 0 ; i < _opts.objects ; ++i )
            {
                motor::math::vec3f_t const pos(
                    ( float_t( i % columns ) - float_t( columns - 1 ) * 0.5f ) * spacing,
                    ( float_t( i / columns ) - float_t( rows - 1 ) * 0.5f ) * spacing, 0.0f ) ;
                motor::math::m3d::trafof_t const transform(
                    motor::math::m3d::trafof_t( motor::math::vec3f_t( spacing * 0.27f ),
                        motor::math::vec3f_t( 0.3f, 0.5f, 0.0f ), pos ) ) ;
                auto vs = motor::shared( motor::graphics::variable_set_t() ) ;
                float_t const gain = 1.0f / float_t( std::max( size_t(1), _opts.lights ) ) ;
                vs->data_variable<motor::math::vec4f_t>( "base_color" )->set(
                    motor::math::vec4f_t( (0.3f+0.6f*float_t(i%3)/2.0f)*gain, 0.65f*gain, 0.9f*gain, 1.0f ) ) ;
                if( single_pass() )
                {
                    for( size_t light = 0 ; light < _opts.lights ; ++light )
                    {
                        vs->data_variable<motor::math::vec3f_t>( "light_dir_" + motor::to_string( light ) )->set(
                            light_direction( light ) ) ;
                    }
                }
                if( direct )
                {
                    auto * world = vs->data_variable<motor::math::mat4f_t>( "world" ) ;
                    world->set( transform.get_transformation() ) ;
                    vs->data_variable<motor::math::mat4f_t>( "view" )->set( _camera.get_view_matrix() ) ;
                    vs->data_variable<motor::math::mat4f_t>( "proj" )->set( _camera.get_proj_matrix() ) ;
                    size_t const id = _shader->add_variable_set( motor::move( vs ) ) ;
                    // Variables are borrowed; the shader owns their variable sets.
                    _direct_objects.emplace_back( direct_object{pos, spacing*0.27f, id, world} ) ;
                    continue ;
                }
                auto leaf = motor::shared( motor::scene::logic_leaf_t() ) ;
                auto tc = motor::shared( motor::scene::trafo3d_component_t( transform ) ) ;
                _transforms.emplace_back( tc ) ;
                leaf->add_component( motor::move( tc ) ) ;
                size_t const id = _shader->add_variable_set( motor::move( vs ) ) ;
                auto comp = motor::shared( motor::scene::msl_component_t( motor::share( _shader ), id, geo ) ) ;
                leaf->add_component( motor::shared( motor::scene::msl_set_component_t( 0, motor::move( comp ) ) ) ) ;
                _root->add_child( motor::move( leaf ) ) ;
            }
            _camera.make_orthographic( 24.0f, 13.5f, 1.0f, 100.0f ) ;
            _camera.look_at( motor::math::vec3f_t(0,0,30), motor::math::vec3f_t(0,1,0), motor::math::vec3f_t(0,0,0) ) ;
        }

        void_t report( void_t ) noexcept
        {
            auto stats = [&]( auto value )
            {
                motor::vector<double_t> values ;
                values.reserve( _samples.size() ) ;
                for( auto const & s : _samples ) values.emplace_back( value(s) ) ;
                std::sort( values.begin(), values.end() ) ;
                size_t const n = values.size() ;
                return std::pair<double_t,double_t>( (values[(n-1)/2]+values[n/2])*0.5,
                    values[size_t(std::ceil(0.95*double_t(n)))-1] ) ;
            } ;
            auto const frame = stats( []( auto const & s ){ return s.frame ; } ) ;
            auto const sync = stats( []( auto const & s ){ return s.sync ; } ) ;
            auto const trafo = stats( []( auto const & s ){ return s.trafo ; } ) ;
            auto const prep0 = stats( []( auto const & s ){ return s.prepare[0] ; } ) ;
            auto const prep1 = stats( []( auto const & s ){ return s.prepare[1] ; } ) ;
            size_t const sets = _shader->borrow_varibale_sets().size() ;
            size_t const expected = _opts.path == "direct" ? _opts.objects :
                _opts.objects * (1 + pass_count()) ;
            *_passed = sets == expected ;
            motor::log::global_t::status<2048>(
                "BENCH,%s,%zu,%zu,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s",
                _opts.backend.c_str(), _opts.objects, _opts.lights, _samples.size(),
                _opts.objects * pass_count() * window_count(), sets,
                frame.first, frame.second, sync.first, sync.second, trafo.first, trafo.second,
                prep0.first, prep0.second, prep1.first, prep1.second, _opts.lighting.c_str() ) ;
            if( !*_passed ) motor::log::global_t::error( "Unexpected variable-set count; benchmark invalid" ) ;
            motor::log::global_t::status( "CPU callback/frame-cadence metrics only; no GPU timestamps or allocation counts. Draw count is expected, not GPU-verified." ) ;
        }

    public:
        benchmark_app( options const & opts, bool_t * passed ) noexcept : _opts(opts), _passed(passed) {}

        void_t on_init( void_t ) noexcept override
        {
            _started = _previous = clock_t::now() ;
            _samples.reserve( _opts.frames ) ;
            make_geometry() ;
            make_scene() ;
            {
                motor::graphics::render_state_sets_t states ;
                states.depth_s.do_change = true ;
                states.depth_s.ss.do_activate = true ;
                states.depth_s.ss.do_depth_write = true ;
                states.depth_s.ss.compare_funk = motor::graphics::depth_compare::less_equal ;
                states.polygon_s.do_change = true ;
                states.polygon_s.ss.do_activate = false ;
                states.blend_s.do_change = true ;
                states.blend_s.ss.do_activate = _opts.lights != 0 && !single_pass() ;
                states.blend_s.ss.blend_func = motor::graphics::blend_function::add ;
                states.blend_s.ss.src_blend_factor = motor::graphics::blend_factor::one ;
                states.blend_s.ss.dst_blend_factor = motor::graphics::blend_factor::one ;
                states.clear_s.do_change = true ;
                states.clear_s.ss.do_activate = true ;
                states.clear_s.ss.do_color_clear = true ;
                states.clear_s.ss.do_depth_clear = true ;
                states.clear_s.ss.clear_color = motor::math::vec4f_t(0,0,0,1) ;
                _state = motor::graphics::state_object_t( "benchmark_state" ) ;
                _state.add_render_state_set( states ) ;
            }
            for( size_t i = 0 ; i < window_count() ; ++i )
            {
                motor::application::window_info_t wi ;
                bool_t const d3d = _opts.backend == "d3d" || i == 1 ;
                wi.window_name = d3d ? "Motor benchmark | D3D11" : "Motor benchmark | GL4" ;
                wi.window_name += " | " + _opts.path + " | " + _opts.lighting + " | " + motor::to_string( _opts.objects ) +
                    " objects | " + motor::to_string( _opts.lights ) + " lights" ;
                wi.x = int_t(20+i*50) ; wi.y = int_t(30+i*50) ; wi.w = 1280 ; wi.h = 720 ;
                wi.gen = d3d ? motor::application::graphics_generation::gen4_d3d11 : motor::application::graphics_generation::gen4_gl4 ;
                _windows[i] = create_window( wi ) ;
                send_window_message( _windows[i], []( auto & w )
                {
                    w.send_message( motor::application::show_message({true}) ) ;
                    w.send_message( motor::application::vsync_message_t({false}) ) ;
                } ) ;
            }
            motor::log::global_t::status<256>( "Benchmark: %s, objects=%zu lights=%zu warmup=%zu samples=%zu",
                _opts.backend.c_str(), _opts.objects, _opts.lights, _opts.warmup, _opts.frames ) ;
            motor::log::global_t::status<128>( "Rendering path: %s", _opts.path.c_str() ) ;
            motor::log::global_t::status<128>( "Lighting: %s, passes per object: %zu",
                _opts.lighting.c_str(), pass_count() ) ;
        }

        void_t on_graphics( motor::application::app::graphics_data_in_t ) noexcept override
        {
            auto const begin = clock_t::now() ;
            _current = measurement{} ;
            _current.frame = ms( _previous, begin ) ;
            _previous = begin ;
            _rendered[0] = _rendered[1] = false ;
            if( _closing || !all_ready() ) return ;
            if( _opts.path == "direct" )
            {
                float_t const angle = float_t( _tick++ % 360 ) * 0.01745329252f ;
                for( auto const & object : _direct_objects )
                {
                    motor::math::m3d::trafof_t const transform( motor::math::vec3f_t(object.scale),
                        motor::math::vec3f_t(0.3f,angle,0.0f), object.position ) ;
                    object.world->set( transform.get_transformation() ) ;
                }
                _current.trafo = ms(begin,clock_t::now()) ;
                return ;
            }
            {
                motor::scene::variable_update_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply( &visitor ) ;
            }
            auto const synced = clock_t::now() ;
            {
                // Fixed time step keeps the workload independent of measured frame rate.
                float_t const angle = float_t( _tick++ % 360 ) * 0.01745329252f ;
                for( auto * tc : _transforms )
                {
                    auto const t = tc->get_trafo() ;
                    tc->set_trafo( motor::math::m3d::trafof_t( t.get_scale(),
                        motor::math::vec3f_t(0.3f,angle,0.0f), t.get_translation() ) ) ;
                }
                motor::scene::trafo_visitor_t visitor ;
                motor::scene::node_t::traverser(_root).apply( &visitor ) ;
            }
            _current.sync = ms(begin,synced) ;
            _current.trafo = ms(synced,clock_t::now()) ;
        }

        void_t on_render( window_id_t const wid, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t data ) noexcept override
        {
            size_t const index = wid == _windows[0] ? 0 : 1 ;
            if( data.first_frame )
            {
                fe->configure<motor::graphics::geometry_object_t>( &_geometry ) ;
                fe->configure<motor::graphics::state_object_t>( &_state ) ;
                fe->configure<motor::graphics::msl_object_t>( _shader ) ;
            }
            if( data.last_frame )
            {
                fe->release<motor::graphics::msl_object_t>( _shader ) ;
                fe->release<motor::graphics::geometry_object_t>( &_geometry ) ;
                fe->release<motor::graphics::state_object_t>( &_state ) ;
                return ;
            }
            auto const status = fe->decode( _shader ) ;
            _ready[index] = status.first == motor::graphics::object_state::ready && status.second == motor::graphics::result::ok ;
            if( !all_ready() || _closing ) return ;
            auto const begin = clock_t::now() ;
            fe->push( &_state ) ;
            if( _opts.path == "direct" )
            {
                for( auto const & object : _direct_objects )
                {
                    motor::graphics::gen4::backend_t::render_detail_t detail ;
                    detail.geo = 0 ;
                    detail.varset = object.varset ;
                    fe->render( _shader, detail ) ;
                }
            }
            else if( _opts.lights == 0 || single_pass() )
            {
                motor::scene::render_visitor_t visitor( 0, 0, fe, &_camera ) ;
                motor::scene::node_t::traverser(_root).apply( &visitor ) ;
            }
            else
            {
                for( size_t light = 0 ; light < _opts.lights ; ++light )
                {
                    motor::gfx::directional_light_t source( light_direction( light ) ) ;
                    motor::scene::light_pass_render_visitor_t visitor( 0, light, fe, &_camera, &source ) ;
                    motor::scene::node_t::traverser(_root).apply( &visitor ) ;
                }
            }
            fe->pop( motor::graphics::gen4::backend::pop_type::render_state ) ;
            _current.prepare[index] = ms(begin,clock_t::now()) ;
            _rendered[index] = true ;
        }

        void_t on_frame_done( void_t ) noexcept override
        {
            if( _closing ) return ;
            if( !_rendered[0] || (window_count()==2 && !_rendered[1]) ) { _warm = 0 ; return ; }
            if( _warm++ < _opts.warmup ) return ;
            _samples.emplace_back( _current ) ;
            if( _samples.size() == _opts.frames )
            {
                report() ; _closing = true ; close() ;
            }
        }
        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        {
            if( !_closing && clock_t::now()-_started > std::chrono::seconds(300) )
                fail( "Benchmark timed out (300 s); no valid result" ) ;
        }
        void_t on_event( window_id_t const, motor::application::window_message_listener::state_vector_cref_t s ) noexcept override
        { if( s.close_changed && !_closing ) fail("Benchmark interrupted by window close") ; }
        void_t on_shutdown( void_t ) noexcept override
        {
            _transforms.clear() ;
            _direct_objects.clear() ;
            if( _root != nullptr ) motor::release( motor::move(_root) ) ;
            motor::release( motor::move(_shader) ) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    bool valid = true ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( arg == "--help" ) { valid = false ; break ; }
        if( i+1 == argc ) { valid = false ; break ; }
        char const * value = argv[++i] ;
        if( arg == "--backend" ) opts.backend = value ;
        else if( arg == "--path" ) opts.path = value ;
        else if( arg == "--lighting" ) opts.lighting = value ;
        else
        {
            size_t number = 0 ;
            auto const parsed = std::from_chars(value,value+std::strlen(value),number) ;
            if( parsed.ec != std::errc() || *parsed.ptr != '\0' ) { valid = false ; break ; }
            if( arg == "--objects" ) opts.objects = number ;
            else if( arg == "--lights" ) opts.lights = number ;
            else if( arg == "--warmup" ) opts.warmup = number ;
            else if( arg == "--frames" ) opts.frames = number ;
            else { valid = false ; break ; }
        }
    }
    valid = valid && opts.objects>0 && opts.objects<=5000 && opts.lights<=8 &&
        (opts.path=="scene" || (opts.path=="direct" && opts.lights==0)) &&
        (opts.lighting=="multipass" || (opts.lighting=="singlepass" && opts.path=="scene" &&
            (opts.lights==2 || opts.lights==3))) &&
        opts.frames>=10 && opts.frames<=10000 && opts.warmup>=2 && opts.warmup<=10000 &&
        (opts.backend=="gl" || opts.backend=="d3d" || opts.backend=="dual") ;
#ifndef _WIN32
    valid = valid && opts.backend=="gl" ;
#endif
    if( !valid )
    {
        motor::log::global_t::status("Usage: 05_scene_benchmark --path scene|direct --lighting multipass|singlepass --backend gl|d3d|dual --objects 1..5000 --lights 0..8 --warmup 2..10000 --frames 10..10000 (direct requires --lights 0; singlepass requires --path scene and --lights 2 or 3)") ;
        motor::log::global_t::deinit() ;
        return argc==2 && motor::string_t(argv[1])=="--help" ? 0 : 2 ;
    }
    bool passed = false ;
    int result = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(
            motor::shared(sample::benchmark_app(opts,&passed)) ) ;
        result = carrier->exec() ;
        motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ;
    motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ;
    motor::log::global_t::deinit() ;
    bool const clean = motor::memory::global_t::dump_to_std()==0 ;
    return result!=0 ? result : (!passed || !clean ? 1 : 0) ;
}
