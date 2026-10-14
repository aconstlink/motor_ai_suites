set( source "00_raylib_benchmark.cpp" )
motor_vs_src_dir( "${source}" )
add_executable( 00_raylib_benchmark "${source}" )
target_link_libraries( 00_raylib_benchmark PRIVATE raylib motor::math motor::log )
set_target_properties( 00_raylib_benchmark PROPERTIES
    FOLDER "suites/suite_raylib"
    VS_DPI_AWARE "PerMonitor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:00_raylib_benchmark>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "--objects 1000 --warmup 120 --frames 300" )

option( MOTOR_AI_RAYLIB_GRAPHICS_TESTS "Register the raylib window smoke test in CTest" OFF )

# The worker comparison is deliberately an explicit GLFW/shared-context adapter,
# not an assumed thread-safe raylib high-level shader API.
add_executable( 01_raylib_compile_cost "01_raylib_compile_cost.cpp" )
target_link_libraries( 01_raylib_compile_cost PRIVATE raylib glfw motor::concurrent motor::log )
set_target_properties( 01_raylib_compile_cost PROPERTIES
    FOLDER "suites/suite_raylib" VS_DPI_AWARE "PerMonitor"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:01_raylib_compile_cost>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "--worker --seed 1" )
if( BUILD_TESTING )
    add_test( NAME raylib_compile_cost_help COMMAND 01_raylib_compile_cost --help )
    set_tests_properties( raylib_compile_cost_help PROPERTIES LABELS "raylib;cpu" TIMEOUT 15 )
    add_test( NAME raylib_benchmark_help COMMAND 00_raylib_benchmark --help )
    add_test( NAME raylib_benchmark_invalid_count COMMAND 00_raylib_benchmark --objects 0 )
    add_test( NAME raylib_benchmark_invalid_matrices COMMAND 00_raylib_benchmark --matrices invalid )
    set_tests_properties( raylib_benchmark_invalid_count raylib_benchmark_invalid_matrices PROPERTIES WILL_FAIL TRUE )
    set_tests_properties( raylib_benchmark_help raylib_benchmark_invalid_count raylib_benchmark_invalid_matrices PROPERTIES
        LABELS "raylib;cpu" TIMEOUT 15 )
    if( MOTOR_AI_RAYLIB_GRAPHICS_TESTS )
        add_test( NAME raylib_benchmark_smoke COMMAND 00_raylib_benchmark --objects 100 --warmup 2 --frames 10 )
        add_test( NAME raylib_benchmark_separate_smoke COMMAND 00_raylib_benchmark --objects 100 --warmup 2 --frames 10 --matrices separate )
        set_tests_properties( raylib_benchmark_smoke raylib_benchmark_separate_smoke PROPERTIES
            LABELS "raylib;graphics" TIMEOUT 60 RUN_SERIAL TRUE )
    endif()
endif()
