set( sources
    "00_vector.cpp"
    "01_matrix.cpp"
    "02_transformation.cpp"
    "03_util.cpp"
    "04_quaternion.cpp"
)

foreach( source IN LISTS sources )
    get_filename_component( app_name "${source}" NAME_WE )
    motor_vs_src_dir( "${source}" )
    add_executable( ${app_name} "${source}" test_support.hpp )
    target_link_libraries( ${app_name} PRIVATE motor::math motor::memory motor::log )
    set_target_properties( ${app_name} PROPERTIES FOLDER "suites/suite_math" )
endforeach()

function( register_math_cases target area )
    if( BUILD_TESTING )
        foreach( scenario IN LISTS ARGN )
            add_test( NAME math_${area}_${scenario} COMMAND ${target} ${scenario} )
            set_tests_properties( math_${area}_${scenario} PROPERTIES
                TIMEOUT 30 LABELS "math;math_${area}" )
        endforeach()
    endif()
endfunction()

register_math_cases( 00_vector vector arithmetic normalization cross )
register_math_cases( 01_matrix matrix products transpose homogeneous rotation2 )
register_math_cases( 02_transformation transformation trs spaces hierarchy camera projection )
register_math_cases( 03_util util scalar rounding angles indexing time basis collinear_basis )
register_math_cases( 04_quaternion quaternion axes composition matrix slerp )
