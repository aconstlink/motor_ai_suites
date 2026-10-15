if( NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" )
    message( FATAL_ERROR "The bgfx reference currently supports Windows and Linux/X11." )
endif()
add_executable( 00_bgfx_benchmark "00_bgfx_benchmark.cpp" )
target_link_libraries( 00_bgfx_benchmark PRIVATE bgfx glfw )
target_compile_definitions( 00_bgfx_benchmark PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN )
motor_reference_target( 00_bgfx_benchmark suite_bgfx )
add_custom_target( bgfx_reference_shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/bin" "$<TARGET_FILE_DIR:00_bgfx_benchmark>/bgfx_shaders"
    VERBATIM )
set_target_properties( bgfx_reference_shaders PROPERTIES FOLDER "suites/suite_bgfx" )
add_dependencies( 00_bgfx_benchmark bgfx_reference_shaders )
if( BUILD_TESTING AND MOTOR_AI_REFERENCE_GRAPHICS_TESTS AND WIN32 )
    add_test( NAME 00_bgfx_benchmark_d3d_smoke COMMAND 00_bgfx_benchmark --backend d3d --objects 100 --warmup 2 --frames 10 )
    set_tests_properties( 00_bgfx_benchmark_d3d_smoke PROPERTIES LABELS "reference;graphics" TIMEOUT 60 RUN_SERIAL TRUE )
endif()
