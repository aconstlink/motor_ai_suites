include( ../reference_common.cmake )
set( MOTOR_AI_CINDER_SOURCE_DIR "" CACHE PATH "Optional existing Cinder source checkout" )
set( CMAKE_POLICY_DEFAULT_CMP0077 NEW )
set( CINDER_DISABLE_AUDIO ON )
set( CINDER_DISABLE_VIDEO ON )
set( CINDER_DISABLE_IMGUI ON )
set( CINDER_BUILD_TESTS OFF )
set( CINDER_BUILD_ALL_SAMPLES OFF )
set( CINDER_BUILD_SAMPLE "" )
set( BUILD_SHARED_LIBS OFF )
set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" )
motor_reference_source( motor_reference_cinder
    "https://codeload.github.com/cinder/Cinder/tar.gz/f90f332d1a4d453282c76435f3401271f25922f9"
    "${MOTOR_AI_CINDER_SOURCE_DIR}" source )
add_subdirectory( "${source}" "${CMAKE_CURRENT_BINARY_DIR}/cinder" EXCLUDE_FROM_ALL )
motor_reference_folders( "${source}" "externals/cinder" )
if( MSVC )
    # Upstream enables unrestricted /MP. Let the top-level build control concurrency.
    get_target_property( options cinder COMPILE_OPTIONS )
    list( FILTER options EXCLUDE REGEX "^/MP[0-9]*$" )
    set_property( TARGET cinder PROPERTY COMPILE_OPTIONS "${options}" )
endif()
# Upstream writes archives into its source tree. Keep all configurations in this build.
foreach( config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL )
    set_target_properties( cinder PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}/lib/${config}"
        LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_CURRENT_BINARY_DIR}/lib/${config}" )
endforeach()
