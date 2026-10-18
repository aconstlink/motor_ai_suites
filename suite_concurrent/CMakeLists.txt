set( sources
    "00_threads_parallel_for.cpp"
    "01_sync_primitives.cpp"
    "02_task_graph.cpp"
    "03_task_storage.cpp"
    "04_datas_locking.cpp"
    "05_datas_cost.cpp"
    "06_variable_set_cost.cpp"
    "07_owner_queue_cost.cpp"
    "08_render_access_cost.cpp"
    "09_work_notify.cpp"
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

target_link_libraries( 06_variable_set_cost PRIVATE motor::graphics )
target_link_libraries( 08_render_access_cost PRIVATE motor::graphics )
target_link_libraries( 09_work_notify PRIVATE motor::graphics )
target_sources( 05_datas_cost PRIVATE
    "support/measured_datas.hpp"
    "support/datas_diagnostics.hpp" )

if( BUILD_TESTING )
    add_test( NAME concurrent_work_notify COMMAND 09_work_notify )
    set_tests_properties( concurrent_work_notify PROPERTIES TIMEOUT 20 LABELS "concurrent;wakeup" RUN_SERIAL TRUE )
    add_test( NAME concurrent_render_access_cost COMMAND 08_render_access_cost --smoke )
    set_tests_properties( concurrent_render_access_cost PROPERTIES TIMEOUT 75 LABELS "concurrent;cost" RUN_SERIAL TRUE )
    add_test( NAME concurrent_variable_set_cost COMMAND 06_variable_set_cost --smoke )
    add_test( NAME concurrent_owner_queue_cost COMMAND 07_owner_queue_cost --smoke )
    set_tests_properties( concurrent_variable_set_cost concurrent_owner_queue_cost PROPERTIES
        TIMEOUT 75 LABELS "concurrent;cost" RUN_SERIAL TRUE )
    add_test( NAME concurrent_datas_cost COMMAND 05_datas_cost --smoke )
    set_tests_properties( concurrent_datas_cost PROPERTIES TIMEOUT 40 LABELS "concurrent;datas" RUN_SERIAL TRUE )
    foreach( scenario lifecycle recursive try_access independent contention wakeup )
        add_test( NAME concurrent_datas_${scenario} COMMAND 04_datas_locking ${scenario} )
        set_tests_properties( concurrent_datas_${scenario} PROPERTIES
            TIMEOUT 30 LABELS "concurrent;concurrent_extended;datas" )
    endforeach()
    foreach( scenario schedule disconnect )
        add_test( NAME concurrent_task_storage_${scenario} COMMAND 03_task_storage ${scenario} )
        set_tests_properties( concurrent_task_storage_${scenario} PROPERTIES
            TIMEOUT 30 LABELS "concurrent;concurrent_extended;task_storage" )
    endforeach()
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
