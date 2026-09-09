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
        add_executable(test_usb_midi_sdk test/usb_midi_sdk.cpp)
        add_executable(test_usb_midi_output test/usb_midi_output.cpp
            src/oc/hal/teensy/UsbMidi.cpp)
        add_executable(test_usb_midi_output_stats test/usb_midi_output.cpp
            src/oc/hal/teensy/UsbMidi.cpp)
        target_compile_definitions(test_usb_midi_output_stats PRIVATE OC_ENABLE_STATS=1)
        set(OC_FRAMEWORK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../framework/src"
            CACHE PATH "OpenControl framework headers for native MIDI tests")
        foreach(target test_usb_midi_output test_usb_midi_output_stats)
            target_include_directories(${target} PRIVATE test/stubs "${OC_FRAMEWORK_INCLUDE_DIR}")
        endforeach()
        foreach(target test_serial_log_buffer test_usb_midi_sdk test_usb_midi_output test_usb_midi_output_stats)
            target_include_directories(${target} PRIVATE src)
            target_compile_features(${target} PRIVATE cxx_std_17)
            if(MSVC)
                target_compile_options(${target} PRIVATE /UNDEBUG)
            else()
                target_compile_options(${target} PRIVATE -UNDEBUG)
            endif()
        endforeach()
        add_test(NAME serial_log_buffer COMMAND test_serial_log_buffer)
        add_test(NAME usb_midi_sdk COMMAND test_usb_midi_sdk)
        add_test(NAME usb_midi_output COMMAND test_usb_midi_output)
        add_test(NAME usb_midi_output_stats COMMAND test_usb_midi_output_stats)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME usb_midi_build_hook
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/usb_midi_build_hook.py")
        add_custom_target(test-native
            COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure
            DEPENDS test_serial_log_buffer test_usb_midi_sdk test_usb_midi_output test_usb_midi_output_stats)
    endif()
endif()
