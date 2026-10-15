#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/gl/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include "workload.hpp"
#include "shaders.hpp"

namespace this_file
{
    using namespace motor::core::types ;
    reference::options opts ;
    int result = 1 ;

    class benchmark : public ci::app::App
    {
        ci::gl::GlslProgRef _shader ;
        ci::gl::BatchRef _batch ;
        motor::vector<reference::object_data> _objects ;
        std::unique_ptr<reference::measurement> _timing ;
        int _world = -1, _view = -1, _projection = -1, _color = -1 ;
        bool_t _capture_ok = true ;
    public:
        static void_t launch_app( ci::app::RendererRef const & renderer )
        {
            ci::app::App::main<benchmark>(renderer,"Cinder benchmark",[]( ci::app::App::Settings * settings )
            {
                settings->setWindowSize(1280,720) ; settings->setResizable(false) ;
                settings->setHighDensityDisplayEnabled(false) ;
                auto const title = "Cinder benchmark | OpenGL | unlit | " + motor::to_string(opts.objects) + " objects" ;
                settings->setTitle(title.c_str()) ;
            }) ;
        }

        void_t setup( void_t ) override
        {
            try
            {
                ci::gl::enableVerticalSync(false) ; disableFrameRate() ;
                if( getWindow()->toPixels(getWindowSize()) != ci::ivec2(1280,720) ) throw std::runtime_error("Framebuffer must be 1280x720") ;
                _shader = ci::gl::GlslProg::create(reference::vertex_shader,reference::fragment_shader) ;
                _world = _shader->getUniformLocation("u_world") ; _view = _shader->getUniformLocation("u_view") ;
                _projection = _shader->getUniformLocation("u_projection") ; _color = _shader->getUniformLocation("u_color") ;
                if( _world < 0 || _view < 0 || _projection < 0 || _color < 0 ) throw std::runtime_error("Missing Cinder uniform") ;
                auto vbo = ci::gl::Vbo::create(GL_ARRAY_BUFFER,sizeof(reference::vertices),reference::vertices,GL_STATIC_DRAW) ;
                auto ibo = ci::gl::Vbo::create(GL_ELEMENT_ARRAY_BUFFER,sizeof(reference::indices),reference::indices,GL_STATIC_DRAW) ;
                ci::geom::BufferLayout layout ;
                layout.append(ci::geom::Attrib::POSITION,3,sizeof(reference::vertex),0) ;
                layout.append(ci::geom::Attrib::NORMAL,3,sizeof(reference::vertex),3*sizeof(float)) ;
                auto mesh = ci::gl::VboMesh::create(24,GL_TRIANGLES,{{layout,vbo}},36,GL_UNSIGNED_INT,ibo) ;
                _batch = ci::gl::Batch::create(mesh,_shader) ;
                _objects = reference::make_objects(opts.objects) ;
                ci::gl::enableDepthRead() ; ci::gl::enableDepthWrite() ; ci::gl::context()->depthFunc(GL_LEQUAL) ;
                ci::gl::disable(GL_CULL_FACE) ; ci::gl::disable(GL_BLEND) ;
                ci::gl::disable(GL_MULTISAMPLE) ; ci::gl::disable(GL_FRAMEBUFFER_SRGB) ;
                if( glGetError() != GL_NO_ERROR ) throw std::runtime_error("Cinder setup GL error") ;
                _timing = std::make_unique<reference::measurement>(opts) ;
            }
            catch( std::exception const & e ) { motor::log::global_t::error(e.what()) ; quit() ; }
        }

        void_t draw( void_t ) override
        {
            if( !_timing ) return ;
            try
            {
                auto const begin = reference::clock_t::now() ;
                if( !_timing->begin(begin) )
                {
                    bool_t const good = _timing->complete() && _capture_ok && glGetError() == GL_NO_ERROR ;
                    if( good ) _timing->report("cinder") ;
                    result = motor::log::global_t::test("Cinder benchmark completed",good) ? 0 : 1 ;
                    quit() ; return ;
                }
                reference::update(_objects,_timing->tick) ;
                auto const updated = reference::clock_t::now() ;
                _timing->current.update = reference::milliseconds(begin,updated) ;
                ci::gl::clear(ci::Color(0,0,0)) ;
                auto const v = reference::view(), p = reference::projection() ;
                for( auto const & object : _objects )
                {
                    _shader->uniform(_world,glm::make_mat4(object.world.data())) ;
                    _shader->uniform(_view,glm::make_mat4(v.data())) ;
                    _shader->uniform(_projection,glm::make_mat4(p.data())) ;
                    _shader->uniform(_color,glm::make_vec4(object.color.data())) ;
                    _batch->draw() ;
                }
                if( _timing->tick == 0 && !opts.capture.empty() )
                {
                    motor::vector<uint8_t> pixels(1280*720*4) ;
                    glReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()) ;
                    _capture_ok = reference::write_ppm(opts.capture.c_str(),1280,720,1280*4,pixels.data(),true,false) ;
                }
                _timing->current.submit = reference::milliseconds(updated,reference::clock_t::now()) ;
                ++_timing->tick ;
            }
            catch( std::exception const & e ) { motor::log::global_t::error(e.what()) ; quit() ; }
        }

        void_t cleanup( void_t ) override
        {
            _batch.reset() ; _shader.reset() ;
            motor::vector<reference::object_data>().swap(_objects) ; _timing.reset() ;
        }
    } ;
}

int main( int argc, char ** argv )
{
    try
    {
        if( argc == 2 && std::strcmp(argv[1],"--help") == 0 ) { reference::usage() ; this_file::result = 0 ; }
        else if( !reference::parse(argc,argv,this_file::opts) ) { reference::usage() ; this_file::result = 2 ; }
        else
        {
            ci::app::Platform::get()->setCommandLineArgs(argc,argv) ;
            auto renderer = std::make_shared<ci::app::RendererGl>(ci::app::RendererGl::Options().version(3,3).msaa(0)) ;
            this_file::benchmark::launch_app(renderer) ;
        }
    }
    catch( std::exception const & e ) { motor::log::global_t::error(e.what()) ; this_file::result = 1 ; }
    motor::log::global_t::deinit() ;
    return this_file::result ;
}
