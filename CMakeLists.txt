cmake_minimum_required(VERSION 3.29)

project(open_control_hal_teensy VERSION 0.2.0 LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenControlHalTeensySources.cmake")

add_library(oc_hal_teensy INTERFACE)
target_sources(oc_hal_teensy INTERFACE ${OC_HAL_TEENSY_SOURCES})
target_include_directories(oc_hal_teensy
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(oc_hal_teensy INTERFACE cxx_std_17)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(test_serial_log_buffer test/serial_log_buffer.cpp)
        target_include_directories(test_serial_log_buffer PRIVATE src)
        target_compile_features(test_serial_log_buffer PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(test_serial_log_buffer PRIVATE /UNDEBUG)
        else()
            target_compile_options(test_serial_log_buffer PRIVATE -UNDEBUG)
        endif()
        add_test(NAME serial_log_buffer COMMAND test_serial_log_buffer)
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            DEPENDS test_serial_log_buffer)
    endif()
endif()
