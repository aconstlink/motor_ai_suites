add_executable( 00_cinder_benchmark "00_cinder_benchmark.cpp" )
target_link_libraries( 00_cinder_benchmark PRIVATE cinder )
motor_reference_target( 00_cinder_benchmark suite_cinder )
