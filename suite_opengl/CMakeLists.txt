add_executable( 00_opengl_benchmark "00_opengl_benchmark.cpp" )
target_link_libraries( 00_opengl_benchmark PRIVATE glfw )
target_include_directories( 00_opengl_benchmark PRIVATE "${PROJECT_SOURCE_DIR}/motor/externals/aconstlink/khronos-header" )
motor_reference_target( 00_opengl_benchmark suite_opengl )
