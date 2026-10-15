if( TARGET glfw )
    return() # Raylib already supplied the window library; never link raylib here.
endif()
include( ../reference_common.cmake )
set( MOTOR_AI_GLFW_SOURCE_DIR "" CACHE PATH "Optional GLFW source checkout for references" )
set( GLFW_BUILD_EXAMPLES OFF )
set( GLFW_BUILD_TESTS OFF )
set( GLFW_BUILD_DOCS OFF )
set( GLFW_INSTALL OFF )
set( GLFW_BUILD_WAYLAND OFF )
set( BUILD_SHARED_LIBS OFF )
set( CMAKE_POLICY_DEFAULT_CMP0077 NEW )
motor_reference_source( motor_reference_glfw
    "https://codeload.github.com/glfw/glfw/tar.gz/3.4"
    "${MOTOR_AI_GLFW_SOURCE_DIR}" source )
add_subdirectory( "${source}" "${CMAKE_CURRENT_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL )
motor_reference_folders( "${source}" "externals/reference_glfw" )
