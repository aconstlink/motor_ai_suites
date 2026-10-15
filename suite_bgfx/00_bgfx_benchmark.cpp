#include <bgfx/bgfx.h>
#include "glfw_window.hpp"
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace this_file
{
    using namespace motor::core::types ;

    std::filesystem::path executable_directory( void_t )
    {
#ifdef _WIN32
        wchar_t path[32768] {} ;
        auto const length = GetModuleFileNameW(nullptr,path,DWORD(std::size(path))) ;
        if( length == 0 || length >= std::size(path) ) throw std::runtime_error("Cannot locate executable") ;
        return std::filesystem::path(path).parent_path() ;
#else
        return std::filesystem::read_symlink("/proc/self/exe").parent_path() ;
#endif
    }

    struct callbacks : bgfx::CallbackI
    {
        std::atomic<bool> captured {false}, capture_ok {false} ;
        void fatal( char const *, uint16_t, bgfx::Fatal::Enum, char const * text ) override
        { motor::log::global_t::error(text) ; std::abort() ; }
        void traceVargs( char const *, uint16_t, char const * format, va_list args ) override
        {
            char text[2048] {} ; std::vsnprintf(text,sizeof(text),format,args) ;
            motor::log::global_t::status(text) ;
        }
        void profilerBegin( char const *, uint32_t, char const *, uint16_t ) override {}
        void profilerBeginLiteral( char const *, uint32_t, char const *, uint16_t ) override {}
        void profilerEnd( void_t ) override {}
        uint32_t cacheReadSize( uint64_t ) override { return 0 ; }
        bool cacheRead( uint64_t, void *, uint32_t ) override { return false ; }
        void cacheWrite( uint64_t, void const *, uint32_t ) override {}
        void screenShot( char const * path, uint32_t w, uint32_t h, uint32_t pitch,
            bgfx::TextureFormat::Enum format, void const * data, uint32_t size, bool flip ) override
        {
            bool const supported = format == bgfx::TextureFormat::BGRA8 || format == bgfx::TextureFormat::RGBA8 ;
            capture_ok = supported && size_t(size) >= size_t(pitch)*h && reference::write_ppm(
                path,w,h,pitch,static_cast<uint8_t const *>(data),flip,format == bgfx::TextureFormat::BGRA8) ;
            captured = true ;
        }
        void captureBegin( uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool ) override {}
        void captureEnd( void_t ) override {}
        void captureFrame( void const *, uint32_t ) override {}
    } ;

    struct session
    {
        bool_t initialized = false ;
        ~session( void_t ) { if( initialized ) bgfx::shutdown() ; }
    } ;

    bgfx::ShaderHandle load_shader( std::filesystem::path const & path )
    {
        std::ifstream file(path,std::ios::binary | std::ios::ate) ;
        if( !file ) throw std::runtime_error("Missing bgfx shader: " + path.string()) ;
        auto const size = file.tellg() ;
        if( size <= 0 || size > 1024*1024 ) throw std::runtime_error("Invalid bgfx shader size") ;
        motor::vector<char> bytes(static_cast<size_t>(size)) ;
        file.seekg(0) ; file.read(bytes.data(),size) ;
        if( !file ) throw std::runtime_error("Cannot read bgfx shader") ;
        auto const shader = bgfx::createShader(bgfx::copy(bytes.data(),uint32_t(bytes.size()))) ;
        if( !bgfx::isValid(shader) ) throw std::runtime_error("Cannot create bgfx shader") ;
        return shader ;
    }

    int run( reference::options const & opts, std::filesystem::path const & shader_dir )
    {
        auto const title = "bgfx benchmark | " + opts.backend + " | unlit | " + motor::to_string(opts.objects) + " objects" ;
        reference::window window(title.c_str(),false) ;
        window.check_size() ;
        callbacks callback ;
        session renderer ;
        bgfx::Init init ;
        init.type = opts.backend == "d3d" ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::OpenGL ;
        init.swapChain.width = 1280 ; init.swapChain.height = 720 ;
        init.swapChain.flags = BGFX_SWAP_CHAIN_NONE ; init.reset = BGFX_RESET_NONE ;
        init.debug = false ; init.profile = false ; init.fallback = false ;
        init.callback = &callback ;
#ifdef _WIN32
        init.swapChain.nwh = glfwGetWin32Window(window.get()) ;
#else
        init.swapChain.ndt = glfwGetX11Display() ;
        init.swapChain.nwh = reinterpret_cast<void *>(uintptr_t(glfwGetX11Window(window.get()))) ;
#endif
        renderer.initialized = bgfx::init(init) ;
        if( !renderer.initialized ) throw std::runtime_error("bgfx initialization failed") ;
        if( bgfx::getRendererType() != init.type ) throw std::runtime_error("bgfx renderer fallback; refusing mismatched timings") ;
        bgfx::VertexLayout layout ;
        layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float).end() ;
        auto const vb = bgfx::createVertexBuffer(bgfx::copy(reference::vertices,sizeof(reference::vertices)),layout) ;
        auto const ib = bgfx::createIndexBuffer(bgfx::copy(reference::indices,sizeof(reference::indices)),BGFX_BUFFER_INDEX32) ;
        auto const vs = load_shader(shader_dir / opts.backend.c_str() / "vertex.bin") ;
        auto const fs = load_shader(shader_dir / opts.backend.c_str() / "fragment.bin") ;
        auto const program = bgfx::createProgram(vs,fs,true) ;
        auto const color = bgfx::createUniform("u_color",bgfx::UniformType::Vec4) ;
        if( !bgfx::isValid(vb) || !bgfx::isValid(ib) || !bgfx::isValid(program) || !bgfx::isValid(color) )
            throw std::runtime_error("bgfx resource creation failed") ;
        auto const v = reference::view(), p = reference::projection(bgfx::getCaps()->homogeneousDepth) ;
        bgfx::setViewRect(0,0,0,1280,720) ;
        bgfx::setViewClear(0,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0x000000ff,1.0f,0) ;
        bgfx::setViewMode(0,bgfx::ViewMode::Sequential) ;
        bgfx::setViewTransform(0,v.data(),p.data()) ;
        auto objects = reference::make_objects(opts.objects) ;
        reference::measurement timing(opts) ;
        bool_t draws_ok = true ;
        while( window.open() )
        {
            auto const begin = reference::clock_t::now() ;
            if( !timing.begin(begin) ) break ;
            reference::update(objects,timing.tick) ;
            auto const updated = reference::clock_t::now() ;
            timing.current.update = reference::milliseconds(begin,updated) ;
            for( auto const & object : objects )
            {
                bgfx::setTransform(object.world.data()) ; bgfx::setUniform(color,object.color.data()) ;
                bgfx::setVertexBuffer(0,vb) ; bgfx::setIndexBuffer(ib) ;
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LEQUAL) ;
                bgfx::submit(0,program) ;
            }
            if( timing.tick == 0 && !opts.capture.empty() )
                bgfx::requestScreenShot(BGFX_INVALID_HANDLE,opts.capture.c_str()) ;
            bgfx::frame() ; glfwPollEvents() ;
            if( timing.tick > opts.warmup && bgfx::getStats()->numDraw != opts.objects )
            {
                if( draws_ok ) motor::log::global_t::error<256>( "Draw count mismatch: expected %zu, received %u", opts.objects, bgfx::getStats()->numDraw ) ;
                draws_ok = false ;
            }
            timing.current.submit = reference::milliseconds(updated,reference::clock_t::now()) ;
            ++timing.tick ;
        }
        bgfx::destroy(color) ; bgfx::destroy(program) ; bgfx::destroy(vb) ; bgfx::destroy(ib) ;
        // Shutdown joins the render thread and completes any screenshot callback.
        bgfx::shutdown() ; renderer.initialized = false ;
        bool_t const good = timing.complete() && draws_ok &&
            (opts.capture.empty() || (callback.captured.load() && callback.capture_ok.load())) ;
        if( good ) timing.report("bgfx") ;
        return motor::log::global_t::test("bgfx benchmark and actual draw count",good) ? 0 : 1 ;
    }
}

int main( int argc, char ** argv )
{
    int result = 0 ;
    try
    {
        reference::options opts ;
        if( argc == 2 && std::strcmp(argv[1],"--help") == 0 ) reference::usage() ;
        else if( !reference::parse(argc,argv,opts,true) ) { reference::usage() ; result = 2 ; }
        else
        {
            // F5 and command-line runs both resolve assets relative to this executable.
            result = this_file::run(opts,this_file::executable_directory() / "bgfx_shaders") ;
        }
    }
    catch( std::exception const & e ) { motor::log::global_t::error(e.what()) ; result = 1 ; }
    motor::log::global_t::deinit() ;
    return result ;
}
