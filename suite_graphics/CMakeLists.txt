set( sources
    "00_triangle.cpp"
    "01_window_lifecycle.cpp"
    "02_dynamic_geometry.cpp"
    "03_shared_scene.cpp"
    "04_reconfigure.cpp"
    "05_scene_benchmark.cpp"
    "06_vertex_pulling.cpp"
    "07_vertex_pulling_field.cpp"
    "08_render_to_texture.cpp"
    "09_geometry_shader.cpp"
    "10_transform_feedback.cpp"
    "11_lighting_scene.cpp"
    "12_scene_wire_lighting.cpp"
    "13_live_recompile.cpp"
    "14_light_components.cpp"
    "15_village.cpp"
    "16_async_assets.cpp"
    "17_debug_primitives.cpp"
    "19_vao_lookup.cpp"
    "20_noise_playground.cpp"
    "21_font_renderer.cpp"
    "23_render_states.cpp"
    "24_buffer_sculpture.cpp"
    "25_gpu_sculpture.cpp"
)

foreach( source IN LISTS sources )
    get_filename_component( app_name "${source}" NAME_WE )
    motor_vs_src_dir( "${source}" )
    add_executable( ${app_name} "${source}" )
    target_link_libraries( ${app_name} PRIVATE
        motor::platform motor::application motor::graphics
        motor::concurrent motor::memory motor::log )
    set_target_properties( ${app_name} PROPERTIES
        FOLDER "suites/suite_graphics" )
endforeach()

target_link_libraries( 05_scene_benchmark PRIVATE motor::scene motor::gfx )
# Match physical render dimensions when comparing with DPI-aware frameworks.
set_property( TARGET 05_scene_benchmark PROPERTY VS_DPI_AWARE "PerMonitor" )

# Capture dimensions must be physical pixels, not DPI-scaled window coordinates.
set( capture_apps 07_vertex_pulling_field 10_transform_feedback 15_village
    20_noise_playground 23_render_states 24_buffer_sculpture 25_gpu_sculpture )
foreach( app IN LISTS capture_apps )
    set_property( TARGET ${app} PROPERTY VS_DPI_AWARE "PerMonitor" )
endforeach()
target_link_libraries( 03_shared_scene PRIVATE motor::gfx )
target_link_libraries( 07_vertex_pulling_field PRIVATE motor::gfx )
target_link_libraries( 24_buffer_sculpture PRIVATE motor::gfx )
target_link_libraries( 25_gpu_sculpture PRIVATE motor::gfx )
target_link_libraries( 08_render_to_texture PRIVATE motor::gfx )
target_link_libraries( 09_geometry_shader PRIVATE motor::gfx )
target_link_libraries( 10_transform_feedback PRIVATE motor::gfx )
target_link_libraries( 11_lighting_scene PRIVATE motor::gfx motor::geometry )
target_link_libraries( 12_scene_wire_lighting PRIVATE motor::scene motor::gfx motor::geometry motor::wire )
target_link_libraries( 14_light_components PRIVATE motor::scene motor::gfx )

target_link_libraries( 15_village PRIVATE motor::scene motor::gfx motor::wire )
target_sources( 15_village PRIVATE village/geometry.hpp village/dragon.hpp village/shaders.hpp )

target_link_libraries( 16_async_assets PRIVATE motor::format motor::io motor::scene motor::gfx motor::wire )
target_compile_definitions( 16_async_assets PRIVATE MOTOR_ASYNC_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets/async_loading" )

target_link_libraries( 17_debug_primitives PRIVATE motor::gfx )

target_link_libraries( 20_noise_playground PRIVATE motor::gfx )

target_link_libraries( 21_font_renderer PRIVATE motor::gfx motor::format motor::io motor::font )
target_compile_definitions( 21_font_renderer PRIVATE
    MOTOR_FONT_ASSETS="${PROJECT_SOURCE_DIR}/motor/externals/ocornut/imgui/misc/fonts" )

add_executable( 22_msl_manager_callbacks
    "22_msl_manager_callbacks.cpp" "22_msl_manager_windows.cpp" )
target_link_libraries( 22_msl_manager_callbacks PRIVATE
    motor::platform motor::application motor::gfx motor::graphics motor::io
    motor::concurrent motor::profiling motor::memory motor::log )
set_target_properties( 22_msl_manager_callbacks PROPERTIES FOLDER "suites/suite_graphics" )
if( BUILD_TESTING )
    add_executable( graphics_capture_options "capture_options_test.cpp" "capture_options.hpp" )
    target_link_libraries( graphics_capture_options PRIVATE motor::memory motor::log )
    set_target_properties( graphics_capture_options PROPERTIES FOLDER "suites/suite_graphics" )
    add_test( NAME graphics_capture_options COMMAND graphics_capture_options )
    set_tests_properties( graphics_capture_options PROPERTIES TIMEOUT 15 LABELS "graphics;capture" )
    foreach( app IN LISTS capture_apps )
        foreach( format IN ITEMS square vertical )
            add_test( NAME graphics_capture_${app}_${format}
                COMMAND ${app} --youtube-short ${format} --help )
            set_tests_properties( graphics_capture_${app}_${format} PROPERTIES
                TIMEOUT 15 LABELS "graphics;capture" )
        endforeach()
        add_test( NAME graphics_capture_${app}_invalid COMMAND ${app} --youtube-short landscape )
        add_test( NAME graphics_capture_${app}_missing COMMAND ${app} --youtube-short )
        set_tests_properties( graphics_capture_${app}_invalid graphics_capture_${app}_missing PROPERTIES
            WILL_FAIL TRUE TIMEOUT 15 LABELS "graphics;capture" )
    endforeach()
    add_test( NAME graphics_gpu_sculpture_cpu COMMAND 25_gpu_sculpture --cpu-only )
    set_tests_properties( graphics_gpu_sculpture_cpu PROPERTIES
        TIMEOUT 30 LABELS "graphics;streamout" )
    add_test( NAME graphics_buffer_sculpture_cpu COMMAND 24_buffer_sculpture --cpu-only )
    set_tests_properties( graphics_buffer_sculpture_cpu PROPERTIES
        TIMEOUT 30 LABELS "graphics;buffers" )
    add_test( NAME graphics_render_states_cpu COMMAND 23_render_states --cpu-only )
    set_tests_properties( graphics_render_states_cpu PROPERTIES
        TIMEOUT 15 LABELS "graphics;states" )
    add_test( NAME graphics_msl_manager_callbacks COMMAND 22_msl_manager_callbacks )
    set_tests_properties( graphics_msl_manager_callbacks PROPERTIES
        TIMEOUT 30 LABELS "graphics;callbacks" )
endif()

if( WIN32 )
    add_executable( 18_carrier_wakeup "18_carrier_wakeup.cpp" )
    target_link_libraries( 18_carrier_wakeup PRIVATE
        motor::platform motor::application motor::concurrent motor::memory motor::log )
    set_target_properties( 18_carrier_wakeup PROPERTIES FOLDER "suites/suite_graphics" )
    if( BUILD_TESTING )
        add_executable( graphics_win32_client_size win32_client_size_test.cpp )
        target_link_libraries( graphics_win32_client_size PRIVATE
            motor::platform motor::application motor::concurrent motor::memory motor::log )
        set_target_properties( graphics_win32_client_size PROPERTIES
            FOLDER "suites/suite_graphics" VS_DPI_AWARE "PerMonitor" )
        add_test( NAME graphics_win32_client_size COMMAND graphics_win32_client_size )
        add_test( NAME graphics_win32_client_size_dpi_unaware COMMAND graphics_win32_client_size --dpi-unaware )
        set_tests_properties( graphics_win32_client_size graphics_win32_client_size_dpi_unaware PROPERTIES
            TIMEOUT 45 LABELS "graphics;window;capture" RUN_SERIAL TRUE )

        add_test( NAME graphics_carrier_wakeup COMMAND 18_carrier_wakeup )
        set_tests_properties( graphics_carrier_wakeup PROPERTIES TIMEOUT 15 LABELS "graphics;wakeup" RUN_SERIAL TRUE )
    endif()
endif()
