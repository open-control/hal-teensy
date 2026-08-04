# Canonical Teensy library sources. The repository's src/main.cpp is a
# standalone smoke application and is intentionally not part of this list.
set(OC_HAL_TEENSY_SOURCE_PATHS
    src/oc/hal/teensy/BuiltInSD.cpp
    src/oc/hal/teensy/HighResolutionClock.cpp
    src/oc/hal/teensy/Ili9341.cpp
    src/oc/hal/teensy/QualificationTelemetry.cpp
    src/oc/hal/teensy/SDCardBackend.cpp
    src/oc/hal/teensy/SDFileSystemBackend.cpp
    src/oc/hal/teensy/UsbMidi.cpp
)

set(OC_HAL_TEENSY_SOURCES ${OC_HAL_TEENSY_SOURCE_PATHS})
list(TRANSFORM OC_HAL_TEENSY_SOURCES
    PREPEND "${CMAKE_CURRENT_LIST_DIR}/../")
