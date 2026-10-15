#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "workload.hpp"

namespace reference
{
    class window
    {
        GLFWwindow * _window = nullptr ;
    public:
        explicit window( char const * title, bool_t const opengl )
        {
            glfwSetErrorCallback( []( int, char const * text ) { motor::log::global_t::error(text) ; } ) ;
            if( !glfwInit() ) throw std::runtime_error("GLFW initialization failed") ;
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE) ;
            if( opengl )
            {
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3) ;
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3) ;
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE) ;
                glfwWindowHint(GLFW_SAMPLES, 0) ;
                glfwWindowHint(GLFW_DEPTH_BITS, 24) ;
            }
            else glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API) ;
            _window = glfwCreateWindow(1280,720,title,nullptr,nullptr) ;
            if( !_window ) { glfwTerminate() ; throw std::runtime_error("Window creation failed") ; }
            if( opengl ) { glfwMakeContextCurrent(_window) ; glfwSwapInterval(0) ; }
        }
        ~window( void_t ) { glfwDestroyWindow(_window) ; glfwTerminate() ; }
        window( window const & ) = delete ;
        window & operator=( window const & ) = delete ;
        GLFWwindow * get( void_t ) const { return _window ; }
        bool_t open( void_t ) const { return !glfwWindowShouldClose(_window) ; }
        void_t check_size( void_t ) const
        {
            int w = 0, h = 0 ;
            glfwGetFramebufferSize(_window, &w, &h) ;
            if( w != 1280 || h != 720 ) throw std::runtime_error("Framebuffer must be 1280x720; refusing mismatched timings") ;
        }
    } ;
}
