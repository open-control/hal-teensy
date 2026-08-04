cmake_minimum_required(VERSION 3.29)

project(open_control_hal_teensy VERSION 0.2.0 LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenControlHalTeensySources.cmake")

add_library(oc_hal_teensy INTERFACE)
target_sources(oc_hal_teensy INTERFACE ${OC_HAL_TEENSY_SOURCES})
target_include_directories(oc_hal_teensy
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(oc_hal_teensy INTERFACE cxx_std_17)
