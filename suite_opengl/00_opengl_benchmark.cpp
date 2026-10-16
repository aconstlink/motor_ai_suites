#include <GL/glcorearb.h>
#include "glfw_window.hpp"
#include "shaders.hpp"

namespace this_file
{
    using namespace motor::core::types ;

    // Resolve only the entry points this reference needs. No motor graphics backend.
    struct gl_api
    {
#define GL_ENTRY(type, name) type name = reinterpret_cast<type>(glfwGetProcAddress("gl" #name))
        GL_ENTRY(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) ;
        GL_ENTRY(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) ;
        GL_ENTRY(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) ;
        GL_ENTRY(PFNGLGENBUFFERSPROC, GenBuffers) ;
        GL_ENTRY(PFNGLBINDBUFFERPROC, BindBuffer) ;
        GL_ENTRY(PFNGLBUFFERDATAPROC, BufferData) ;
        GL_ENTRY(PFNGLDELETEBUFFERSPROC, DeleteBuffers) ;
        GL_ENTRY(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) ;
        GL_ENTRY(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) ;
        GL_ENTRY(PFNGLCREATESHADERPROC, CreateShader) ;
        GL_ENTRY(PFNGLSHADERSOURCEPROC, ShaderSource) ;
        GL_ENTRY(PFNGLCOMPILESHADERPROC, CompileShader) ;
        GL_ENTRY(PFNGLGETSHADERIVPROC, GetShaderiv) ;
        GL_ENTRY(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) ;
        GL_ENTRY(PFNGLDELETESHADERPROC, DeleteShader) ;
        GL_ENTRY(PFNGLCREATEPROGRAMPROC, CreateProgram) ;
        GL_ENTRY(PFNGLATTACHSHADERPROC, AttachShader) ;
        GL_ENTRY(PFNGLLINKPROGRAMPROC, LinkProgram) ;
        GL_ENTRY(PFNGLGETPROGRAMIVPROC, GetProgramiv) ;
        GL_ENTRY(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) ;
        GL_ENTRY(PFNGLDELETEPROGRAMPROC, DeleteProgram) ;
        GL_ENTRY(PFNGLUSEPROGRAMPROC, UseProgram) ;
        GL_ENTRY(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) ;
        GL_ENTRY(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) ;
        GL_ENTRY(PFNGLUNIFORM4FVPROC, Uniform4fv) ;
        GL_ENTRY(PFNGLDRAWELEMENTSPROC, DrawElements) ;
        GL_ENTRY(PFNGLCLEARCOLORPROC, ClearColor) ;
        GL_ENTRY(PFNGLCLEARPROC, Clear) ;
        GL_ENTRY(PFNGLENABLEPROC, Enable) ;
        GL_ENTRY(PFNGLDISABLEPROC, Disable) ;
        GL_ENTRY(PFNGLDEPTHFUNCPROC, DepthFunc) ;
        GL_ENTRY(PFNGLVIEWPORTPROC, Viewport) ;
        GL_ENTRY(PFNGLREADPIXELSPROC, ReadPixels) ;
        GL_ENTRY(PFNGLGETERRORPROC, GetError) ;
        GL_ENTRY(PFNGLGETSTRINGPROC, GetString) ;
#undef GL_ENTRY
        bool_t valid( void_t ) const
        {
            return GenVertexArrays && BindVertexArray && DeleteVertexArrays && GenBuffers && BindBuffer && BufferData &&
                DeleteBuffers && VertexAttribPointer && EnableVertexAttribArray && CreateShader && ShaderSource &&
                CompileShader && GetShaderiv && GetShaderInfoLog && DeleteShader && CreateProgram && AttachShader &&
                LinkProgram && GetProgramiv && GetProgramInfoLog && DeleteProgram && UseProgram && GetUniformLocation &&
                UniformMatrix4fv && Uniform4fv && DrawElements && ClearColor && Clear && Enable && Disable &&
                DepthFunc && Viewport && ReadPixels && GetError && GetString ;
        }
    } ;

    GLuint compile( gl_api const & gl, GLenum stage, char const * source )
    {
        GLuint const shader = gl.CreateShader(stage) ;
        gl.ShaderSource(shader, 1, &source, nullptr) ;
        gl.CompileShader(shader) ;
        GLint good = 0 ;
        gl.GetShaderiv(shader, GL_COMPILE_STATUS, &good) ;
        if( !good )
        {
            char text[4096] {} ;
            gl.GetShaderInfoLog(shader, sizeof(text), nullptr, text) ;
            gl.DeleteShader(shader) ;
            throw std::runtime_error(text) ;
        }
        return shader ;
    }

    int run( reference::options const & opts )
    {
        auto const title = "OpenGL benchmark | direct | unlit | " + motor::to_string(opts.objects) + " objects" ;
        reference::window window(title.c_str(), true) ;
        window.check_size() ;
        gl_api gl ;
        if( !gl.valid() ) throw std::runtime_error("Missing OpenGL entry point") ;
        motor::log::global_t::status<512>( "GL_RENDERER,%s,%s", gl.GetString(GL_RENDERER), gl.GetString(GL_VERSION) ) ;
        GLuint const vs = compile(gl,GL_VERTEX_SHADER,reference::vertex_shader) ;
        GLuint const fs = compile(gl,GL_FRAGMENT_SHADER,reference::fragment_shader) ;
        GLuint const program = gl.CreateProgram() ;
        gl.AttachShader(program,vs) ; gl.AttachShader(program,fs) ; gl.LinkProgram(program) ;
        gl.DeleteShader(vs) ; gl.DeleteShader(fs) ;
        GLint linked = 0 ; gl.GetProgramiv(program,GL_LINK_STATUS,&linked) ;
        if( !linked )
        {
            char text[4096] {} ; gl.GetProgramInfoLog(program,sizeof(text),nullptr,text) ;
            throw std::runtime_error(text) ;
        }
        GLint const world = gl.GetUniformLocation(program,"u_world"), view = gl.GetUniformLocation(program,"u_view"),
            projection = gl.GetUniformLocation(program,"u_projection"), color = gl.GetUniformLocation(program,"u_color") ;
        if( world < 0 || view < 0 || projection < 0 || color < 0 ) throw std::runtime_error("Missing uniform") ;
        GLuint vao = 0, buffers[2] {} ;
        gl.GenVertexArrays(1,&vao) ; gl.BindVertexArray(vao) ; gl.GenBuffers(2,buffers) ;
        gl.BindBuffer(GL_ARRAY_BUFFER,buffers[0]) ;
        gl.BufferData(GL_ARRAY_BUFFER,sizeof(reference::vertices),reference::vertices,GL_STATIC_DRAW) ;
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]) ;
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(reference::indices),reference::indices,GL_STATIC_DRAW) ;
        gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(reference::vertex),nullptr) ; gl.EnableVertexAttribArray(0) ;
        gl.Viewport(0,0,1280,720) ; gl.ClearColor(0,0,0,1) ; gl.Enable(GL_DEPTH_TEST) ; gl.DepthFunc(GL_LEQUAL) ;
        gl.Disable(GL_CULL_FACE) ; gl.Disable(GL_BLEND) ; gl.Disable(GL_MULTISAMPLE) ; gl.Disable(GL_FRAMEBUFFER_SRGB) ;
        if( gl.GetError() != GL_NO_ERROR ) throw std::runtime_error("OpenGL setup error") ;

        auto objects = reference::make_objects(opts.objects) ;
        auto const v = reference::view(), p = reference::projection() ;
        reference::measurement timing(opts) ;
        bool_t capture_ok = true ;
        while( window.open() )
        {
            auto const begin = reference::clock_t::now() ;
            if( !timing.begin(begin) ) break ;
            reference::update(objects,timing.tick) ;
            auto const updated = reference::clock_t::now() ;
            timing.current.update = reference::milliseconds(begin,updated) ;
            gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) ;
            gl.UseProgram(program) ; gl.BindVertexArray(vao) ;
            for( auto const & object : objects )
            {
                gl.UniformMatrix4fv(world,1,GL_FALSE,object.world.data()) ;
                gl.UniformMatrix4fv(view,1,GL_FALSE,v.data()) ;
                gl.UniformMatrix4fv(projection,1,GL_FALSE,p.data()) ;
                gl.Uniform4fv(color,1,object.color.data()) ;
                gl.DrawElements(GL_TRIANGLES,36,GL_UNSIGNED_INT,nullptr) ;
            }
            if( timing.tick == 0 && !opts.capture.empty() )
            {
                motor::vector<uint8_t> pixels(1280*720*4) ;
                gl.ReadPixels(0,0,1280,720,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()) ;
                capture_ok = reference::write_ppm(opts.capture.c_str(),1280,720,1280*4,pixels.data(),true,false) ;
            }
            glfwSwapBuffers(window.get()) ; glfwPollEvents() ;
            timing.current.submit = reference::milliseconds(updated,reference::clock_t::now()) ;
            ++timing.tick ;
        }
        bool_t const good = timing.complete() && capture_ok && gl.GetError() == GL_NO_ERROR ;
        if( good ) timing.report("opengl") ;
        gl.DeleteBuffers(2,buffers) ; gl.DeleteVertexArrays(1,&vao) ; gl.DeleteProgram(program) ;
        return motor::log::global_t::test("Direct OpenGL benchmark completed",good) ? 0 : 1 ;
    }
}

int main( int argc, char ** argv )
{
    int result = 0 ;
    try
    {
        reference::options opts ;
        if( argc == 2 && std::strcmp(argv[1],"--help") == 0 ) reference::usage() ;
        else if( !reference::parse(argc,argv,opts) ) { reference::usage() ; result = 2 ; }
        else result = this_file::run(opts) ;
    }
    catch( std::exception const & e ) { motor::log::global_t::error(e.what()) ; result = 1 ; }
    motor::log::global_t::deinit() ;
    return result ;
}
