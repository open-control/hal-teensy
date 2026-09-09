cmake_minimum_required(VERSION 3.29)
project(sd_filesystem_contract LANGUAGES CXX)
enable_testing()
set(OC_SDFAT_SOURCE_DIR "" CACHE PATH "Installed Teensy SDK libraries/SdFat/src")
set(OC_HAL_SD_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/oc/hal/teensy" CACHE PATH "SD backend sources")
if(NOT EXISTS "${OC_SDFAT_SOURCE_DIR}/FsLib/FsFile.cpp")
    message(FATAL_ERROR "Set OC_SDFAT_SOURCE_DIR to the installed Teensy SdFat sources")
endif()
# Use the real FAT/exFAT implementation; only the Arduino presentation layer is disabled.
# The installed SDK is never edited. No vendored copy is committed.
set(sdk "${CMAKE_CURRENT_BINARY_DIR}/sdfat")
file(COPY "${OC_SDFAT_SOURCE_DIR}/" DESTINATION "${sdk}")
file(READ "${sdk}/SdFatConfig.h" config)
foreach(feature ENABLE_ARDUINO_FEATURES ENABLE_ARDUINO_SERIAL ENABLE_ARDUINO_STRING)
    string(REPLACE "#define ${feature} 1" "#define ${feature} 0" config "${config}")
endforeach()
file(WRITE "${sdk}/SdFatConfig.h" "#include \"host_platform.hpp\"\n${config}")
file(READ "${sdk}/common/FsBlockDevice.h" block_device)
string(REPLACE "#include \"SdCard/SdCard.h\"" "#include \"../SdFatConfig.h\"\n#include \"FsBlockDeviceInterface.h\"" block_device "${block_device}")
file(WRITE "${sdk}/common/FsBlockDevice.h" "${block_device}")
file(GLOB sdk_sources "${sdk}/FsLib/*.cpp" "${sdk}/FatLib/*.cpp"
    "${sdk}/ExFatLib/*.cpp" "${sdk}/common/*.cpp")
add_executable(test_sd_filesystem test_main.cpp ${sdk_sources}
    "${OC_HAL_SD_SOURCE_DIR}/SDFileSystemBackend.cpp")
# Exercise the exact production PSRAM staging branch with a reserved native address.
set_source_files_properties("${OC_HAL_SD_SOURCE_DIR}/SDFileSystemBackend.cpp"
    PROPERTIES COMPILE_DEFINITIONS __IMXRT1062__)
target_include_directories(test_sd_filesystem PRIVATE stubs "${sdk}"
    "${OC_HAL_SD_SOURCE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../../framework/src")
target_compile_features(test_sd_filesystem PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(test_sd_filesystem PRIVATE /UNDEBUG)
else()
    target_compile_options(test_sd_filesystem PRIVATE -UNDEBUG)
endif()
add_test(NAME sd_filesystem_contract COMMAND test_sd_filesystem)
