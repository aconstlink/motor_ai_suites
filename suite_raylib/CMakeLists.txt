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
if( BUILD_TESTING )
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
