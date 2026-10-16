find_package( Threads REQUIRED )

foreach( source 00_log_store.cpp 01_log_system.cpp )
    get_filename_component( app_name "${source}" NAME_WE )
    motor_vs_src_dir( "${source}" )
    add_executable( ${app_name} "${source}" test_runner.hpp )
    target_link_libraries( ${app_name} PRIVATE motor::log motor::memory Threads::Threads )
    set_target_properties( ${app_name} PROPERTIES FOLDER "suites/suite_log" )
endforeach()

# system/std_cerr_logger are internal, non-exported classes in shared builds.
# Compile the unchanged original sources into this white-box test only.
target_sources( 01_log_system PRIVATE
    "${PROJECT_SOURCE_DIR}/motor/log/system/system.cpp"
    "${PROJECT_SOURCE_DIR}/motor/log/logger/std_cerr_logger.cpp" )

if( BUILD_TESTING )
    foreach( scenario empty order wrap bounded copy_zero copy_range copy_wrap move writers )
        add_test( NAME log_store_${scenario} COMMAND 00_log_store ${scenario} )
        set_tests_properties( log_store_${scenario} PROPERTIES TIMEOUT 20 LABELS "log;log_store" )
    endforeach()
    set_tests_properties( log_store_copy_range log_store_copy_wrap
        PROPERTIES LABELS "log;log_store;log_unsafe" )
    foreach( scenario fanout test_result default move )
        add_test( NAME log_system_${scenario} COMMAND 01_log_system ${scenario} )
        set_tests_properties( log_system_${scenario} PROPERTIES TIMEOUT 20 LABELS "log;log_system" )
    endforeach()
endif()
