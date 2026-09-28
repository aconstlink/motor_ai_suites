set( sources
    "00_triangle.cpp"
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
