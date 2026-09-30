cmake_minimum_required( VERSION 3.20 )
project( motor_ai_suites LANGUAGES C CXX )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" )

add_subdirectory( motor )
list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/motor/cmake/macros" )
include( visual_studio_src_dir )

add_subdirectory( suite_graphics )
include( CTest )
add_subdirectory( suite_concurrent )
add_subdirectory( suite_wire )
set_property( DIRECTORY PROPERTY VS_STARTUP_PROJECT 00_triangle )
