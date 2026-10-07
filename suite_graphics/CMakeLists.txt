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
target_link_libraries( 03_shared_scene PRIVATE motor::gfx )
target_link_libraries( 07_vertex_pulling_field PRIVATE motor::gfx )
target_link_libraries( 08_render_to_texture PRIVATE motor::gfx )
target_link_libraries( 09_geometry_shader PRIVATE motor::gfx )
target_link_libraries( 10_transform_feedback PRIVATE motor::gfx )
target_link_libraries( 11_lighting_scene PRIVATE motor::gfx motor::geometry )
target_link_libraries( 12_scene_wire_lighting PRIVATE motor::scene motor::gfx motor::geometry motor::wire )
