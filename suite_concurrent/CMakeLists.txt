set( sources
    "00_threads_parallel_for.cpp"
    "01_sync_primitives.cpp"
    "02_task_graph.cpp"
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
    foreach( scenario semaphore signal readers contention )
        add_test( NAME concurrent_sync_${scenario} COMMAND 01_sync_primitives ${scenario} )
        set_tests_properties( concurrent_sync_${scenario} PROPERTIES
            TIMEOUT 30 LABELS "concurrent;concurrent_extended" )
    endforeach()
    foreach( scheduler pool loose )
        foreach( scenario chain diamond fanout )
            add_test( NAME concurrent_graph_${scheduler}_${scenario}
                COMMAND 02_task_graph ${scheduler} ${scenario} )
            set_tests_properties( concurrent_graph_${scheduler}_${scenario} PROPERTIES
                TIMEOUT 30 LABELS "concurrent;concurrent_extended" RUN_SERIAL TRUE )
        endforeach()
    endforeach()
    foreach( scenario threads pool ranges remainder nested )
        add_test( NAME concurrent_${scenario}
            COMMAND 00_threads_parallel_for ${scenario} )
        set_tests_properties( concurrent_${scenario} PROPERTIES
            TIMEOUT 90 LABELS concurrent RUN_SERIAL TRUE )
    endforeach()
endif()
