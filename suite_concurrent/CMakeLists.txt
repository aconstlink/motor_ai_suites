set( sources
    "00_threads_parallel_for.cpp"
)

find_package( Threads REQUIRED )

foreach( source IN LISTS sources )
    get_filename_component( app_name "${source}" NAME_WE )
    motor_vs_src_dir( "${source}" )
    add_executable( ${app_name} "${source}" )
    target_link_libraries( ${app_name} PRIVATE
        motor::concurrent motor::memory motor::log Threads::Threads )
    set_target_properties( ${app_name} PROPERTIES FOLDER "suites/suite_concurrent" )
endforeach()

if( BUILD_TESTING )
    foreach( scenario threads pool ranges remainder nested )
        add_test( NAME concurrent_${scenario}
            COMMAND 00_threads_parallel_for ${scenario} )
        set_tests_properties( concurrent_${scenario} PROPERTIES
            TIMEOUT 90 LABELS concurrent RUN_SERIAL TRUE )
    endforeach()
endif()
