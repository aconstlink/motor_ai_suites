// vertex_buffer::resize computes a floating-point ratio against the initial
// zero element count. MSVC diagnoses the inlined setup path under /WX.
#if defined( _MSC_VER )
#pragma warning( disable : 4723 )
#endif

#include <motor/platform/global.h>
#include <motor/graphics/frontend/gen4/frontend.hpp>
#include <motor/graphics/object/geometry_object.h>
#include <motor/graphics/object/msl_object.h>
#include <motor/graphics/object/state_object.h>
#include <motor/concurrent/global.h>
#include <motor/log/global.h>
#include <motor/memory/global.h>
#include <motor/profiling/global.h>
#include <motor/io/global.h>
#include <motor/std/vector>
#include <motor/std/string>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstring>
#include <utility>

namespace sample
{
    using namespace motor::core::types ;
    using clock_t = std::chrono::steady_clock ;
    struct options { size_t geometries = 256, draws = 4096, warmup = 80, frames = 400 ; } ;
    double_t elapsed( clock_t::time_point const a, clock_t::time_point const b ) noexcept
    { return std::chrono::duration<double_t,std::milli>(b-a).count() ; }

    class vao_app : public motor::application::app
    {
        options _opts ;
        bool_t * _passed ;
        motor::vector<motor::graphics::geometry_object_mtr_t> _geometry ;
        motor::vector<size_t> _links ;
        motor::graphics::msl_object_mtr_t _shader = nullptr ;
        motor::graphics::state_object_t _state ;
        motor::vector<double_t> _backend_ms, _frame_ms ;
        std::atomic<size_t> * _completed ;
        size_t _submitted = 0 ;
        bool_t _closing = false ;
        clock_t::time_point _started = clock_t::now(), _previous, _draw_start ;

        void_t fail( char_cptr_t message ) noexcept
        { motor::log::global_t::test(message,false) ; _closing = true ; close() ; }

        void_t report( void_t ) noexcept
        {
            struct summary { double_t mean, median, p95 ; } ;
            auto stats = []( motor::vector<double_t> values )
            {
                if( values.empty() ) return summary{0,0,0} ;
                double_t sum = 0 ; for( auto v : values ) sum += v ;
                std::sort(values.begin(),values.end()) ;
                return summary{sum/double_t(values.size()),
                    values[values.size()/2],values[(values.size()-1)*95/100]} ;
            } ;
            auto const backend = stats(_backend_ms), frame = stats(_frame_ms) ;
            motor::vector<double_t> even, odd, pair_difference ;
            for( size_t i = 0 ; i+1 < _backend_ms.size() ; i += 2 )
            {
                even.push_back(_backend_ms[i]) ; odd.push_back(_backend_ms[i+1]) ;
                pair_difference.push_back(_backend_ms[i]-_backend_ms[i+1]) ;
            }
            auto const a = stats(even), b = stats(odd), delta = stats(pair_difference) ;
            motor::log::global_t::status<512>(
                "VAO_PAIR_SAMPLES even_backend_mean_ms=%.6f odd_backend_mean_ms=%.6f pair_delta_mean_ms=%.6f pair_delta_median_ms=%.6f pairs=%zu",
                a.mean,b.mean,delta.mean,delta.median,pair_difference.size()) ;
            motor::log::global_t::status<1024>(
                "VAO_BENCH geometries=%zu draws=%zu warmup=%zu frames=%zu vsync=0 width=640 height=480 backend_mean_ms=%.6f backend_median_ms=%.6f backend_p95_ms=%.6f frame_mean_ms=%.6f frame_median_ms=%.6f frame_p95_ms=%.6f",
                _opts.geometries,_opts.draws,_opts.warmup,_opts.frames,
                backend.mean,backend.median,backend.p95,frame.mean,frame.median,frame.p95) ;
            motor::log::global_t::test("[vao] all measured draw batches completed",true) ;
            *_passed = true ; _closing = true ; close() ;
        }

    public:
        vao_app( options const opts, bool_t * passed, std::atomic<size_t> * completed ) noexcept :
            _opts(opts), _passed(passed), _completed(completed) {}

        void_t on_init( void_t ) noexcept override
        {
            _backend_ms.resize(_opts.frames) ; _frame_ms.resize(_opts.frames) ;
            _geometry.reserve(_opts.geometries) ; _links.reserve(_opts.geometries) ;
            struct vertex { motor::math::vec3f_t pos ; } ;
            for( size_t i = 0 ; i < _opts.geometries ; ++i )
            {
                // Identical, tiny on-screen triangles keep fragment work constant.
                motor::graphics::vertex_buffer_t vb ;
                vb.add_layout_element(
                    motor::graphics::vertex_attribute::position,motor::graphics::type::tfloat,
                    motor::graphics::type_struct::vec3).resize(3).update<vertex>([]( vertex * v, size_t const )
                    {
                        v[0].pos = motor::math::vec3f_t(-0.01f,-0.01f,0) ;
                        v[1].pos = motor::math::vec3f_t(0,0.01f,0) ;
                        v[2].pos = motor::math::vec3f_t(0.01f,-0.01f,0) ;
                    }) ;
                motor::graphics::index_buffer_t ib ;
                ib.set_layout_element(motor::graphics::type::tuint)
                    .resize(3).update<uint_t>([]( uint_t * v, size_t const ){ v[0]=0 ; v[1]=1 ; v[2]=2 ; }) ;
                _geometry.push_back(motor::shared(motor::graphics::geometry_object_t(
                    "vao_geometry_"+motor::to_string(i),
                    motor::graphics::primitive_type::triangles,std::move(vb),std::move(ib)))) ;
            }
            {
                motor::graphics::msl_object_t shader("vao_shader") ;
                shader.add(motor::graphics::msl_api_type::msl_4_0,R"(
                    config vao_shader
                    {
                        vertex_shader
                        {
                            in vec3_t pos : position ; out vec4_t pos : position ;
                            void main() { out.pos = vec4_t(in.pos,1.0) ; }
                        }
                        pixel_shader
                        {
                            out vec4_t color : color ;
                            void main() { out.color = vec4_t(0.2,0.8,0.4,1.0) ; }
                        }
                    })") ;
                for( auto const & geo : _geometry ) _links.push_back(shader.link_geometry(geo->name())) ;
                shader.add_variable_set(motor::shared(motor::graphics::variable_set_t())) ;
                _shader = motor::shared(std::move(shader)) ;
            }
            {
                motor::graphics::render_state_sets_t s ;
                s.depth_s.do_change = true ; s.depth_s.ss.do_activate = false ; s.depth_s.ss.do_depth_write = false ;
                s.polygon_s.do_change = true ; s.polygon_s.ss.do_activate = false ;
                s.blend_s.do_change = true ; s.blend_s.ss.do_activate = false ;
                s.clear_s.do_change = true ; s.clear_s.ss.do_activate = true ; s.clear_s.ss.do_color_clear = true ;
                s.clear_s.ss.clear_color = motor::math::vec4f_t(0.06f,0.06f,0.06f,1) ;
                _state = motor::graphics::state_object_t("vao_state") ; _state.add_render_state_set(s) ;
            }
            {
                motor::application::window_info_t info ;
                info.window_name = "motor | VAO lookup | "+motor::to_string(_opts.geometries)+" links / "+motor::to_string(_opts.draws)+" draws" ;
                info.w = 640 ; info.h = 480 ; info.x = 50 ; info.y = 50 ;
                info.gen = motor::application::graphics_generation::gen4_gl4 ;
                send_window_message(create_window(info),[]( auto & window )
                {
                    window.send_message(motor::application::show_message({true})) ;
                    window.send_message(motor::application::vsync_message_t({false})) ;
                }) ;
            }
        }

        void_t on_first_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            for( auto & geo : _geometry ) fe->configure<motor::graphics::geometry_object_t>(geo) ;
            fe->configure<motor::graphics::state_object_t>(&_state) ;
            fe->configure<motor::graphics::msl_object_t>(_shader) ;
        }

        void_t on_render( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            if( _closing ) return ;
            if( _completed->load(std::memory_order_acquire) == _opts.warmup+_opts.frames ) { report() ; return ; }
            auto const status = fe->decode(_shader) ;
            if( status.first != motor::graphics::object_state::ready || status.second != motor::graphics::result::ok ) return ;
            if( _submitted == _opts.warmup+_opts.frames ) return ;
            auto const now = clock_t::now() ;
            size_t const frame = _submitted++ ;
            if( frame >= _opts.warmup ) _frame_ms[frame-_opts.warmup] = elapsed(_previous,now) ;
            _previous = now ;
            fe->push(&_state) ;
            fe->fence([this]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase == motor::graphics::gen4::frontend::fence_phase::execute ) _draw_start = clock_t::now() ;
            }) ;
            for( size_t i = 0 ; i < _opts.draws ; ++i )
            {
                motor::graphics::gen4::backend::render_detail_t detail ;
                detail.geo = _links[i%_opts.geometries] ; detail.varset = 0 ;
                fe->render(_shader,detail) ;
            }
            fe->fence([this,frame]( motor::graphics::gen4::frontend::fence_phase const phase )
            {
                if( phase != motor::graphics::gen4::frontend::fence_phase::execute ) return ;
                if( frame >= _opts.warmup ) _backend_ms[frame-_opts.warmup] = elapsed(_draw_start,clock_t::now()) ;
                _completed->store(frame+1,std::memory_order_release) ;
            }) ;
            fe->pop(motor::graphics::gen4::backend::pop_type::render_state) ;
        }

        void_t on_last_frame( window_id_t const, motor::graphics::gen4::frontend_ptr_t fe,
            motor::application::app::render_data_in_t ) noexcept override
        {
            fe->release<motor::graphics::msl_object_t>(_shader) ;
            for( auto & geo : _geometry ) fe->release<motor::graphics::geometry_object_t>(geo) ;
            fe->release<motor::graphics::state_object_t>(&_state) ;
        }

        void_t on_update( motor::application::app::update_data_in_t ) noexcept override
        { if( !_closing && clock_t::now()-_started > std::chrono::seconds(90) ) fail("[vao] timeout") ; }
        void_t on_event( window_id_t const, motor::application::window_message_listener::state_vector_cref_t state ) noexcept override
        { if( state.close_changed && !_closing ) fail("[vao] interrupted; no valid measurement") ; }
        void_t on_shutdown( void_t ) noexcept override
        {
            motor::release(motor::move(_shader)) ;
            for( auto & geo : _geometry ) motor::release(motor::move(geo)) ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    sample::options opts ;
    for( int i = 1 ; i < argc ; ++i )
    {
        motor::string_t const arg(argv[i]) ;
        if( i+1 == argc ) return 2 ;
        auto const value = argv[++i] ; size_t number = 0 ;
        auto const end = value+std::strlen(value) ; auto const parsed = std::from_chars(value,end,number) ;
        if( parsed.ec != std::errc() || parsed.ptr != end ) return 2 ;
        if( arg == "--geometries" ) opts.geometries = number ;
        else if( arg == "--draws" ) opts.draws = number ;
        else if( arg == "--warmup" ) opts.warmup = number ;
        else if( arg == "--frames" ) opts.frames = number ;
        else return 2 ;
    }
    if( opts.geometries == 0 || opts.geometries > 8192 || opts.draws < opts.geometries ||
        opts.draws > 65536 || opts.draws%opts.geometries != 0 || opts.warmup < 2 ||
        opts.warmup > 10000 || opts.frames < 10 || opts.frames > 10000 ) return 2 ;
    bool passed = false ; int result = 0 ;
    std::atomic<size_t> completed = 0 ;
    {
        auto carrier = motor::platform::global_t::create_carrier(motor::shared(sample::vao_app(opts,&passed,&completed))) ;
        result = carrier->exec() ; motor::release(motor::move(carrier)) ;
    }
    motor::concurrent::global_t::deinit() ; motor::profiling::global_t::deinit() ;
    motor::io::global_t::deinit() ; motor::log::global_t::deinit() ;
    auto const remaining = motor::memory::global_t::dump_to_std() ;
    return result != 0 ? result : (!passed || remaining != 0 ? 1 : 0) ;
}
