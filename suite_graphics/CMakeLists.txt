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
target_link_libraries( 03_shared_scene PRIVATE motor::gfx )
target_link_libraries( 07_vertex_pulling_field PRIVATE motor::gfx )
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

if( WIN32 )
    add_executable( 18_carrier_wakeup "18_carrier_wakeup.cpp" )
    target_link_libraries( 18_carrier_wakeup PRIVATE
        motor::platform motor::application motor::concurrent motor::memory motor::log )
    set_target_properties( 18_carrier_wakeup PROPERTIES FOLDER "suites/suite_graphics" )
    if( BUILD_TESTING )
        add_test( NAME graphics_carrier_wakeup COMMAND 18_carrier_wakeup )
        set_tests_properties( graphics_carrier_wakeup PROPERTIES TIMEOUT 15 LABELS "graphics;wakeup" RUN_SERIAL TRUE )
    endif()
endif()
