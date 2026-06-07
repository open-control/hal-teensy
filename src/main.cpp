/**
 * @file main.cpp
 * @brief Compilation test for driver-teensy
 */
#include <Arduino.h>

#include <oc/hal/teensy/EEPROMBackend.hpp>
#include <oc/hal/teensy/EncoderController.hpp>
#include <oc/hal/teensy/Ili9341.hpp>
#include <oc/hal/teensy/LittleFSBackend.hpp>
#include <oc/hal/teensy/SDFileSystemBackend.hpp>
#include <oc/hal/teensy/SDFileSystemSmokeTest.hpp>
#include <oc/hal/teensy/UsbMidi.hpp>

// Verify types compile correctly
using Midi = oc::hal::teensy::UsbMidi;
using EepromStorage = oc::hal::teensy::EEPROMBackend;
using FlashStorage = oc::hal::teensy::LittleFSBackend;
using SDFileSystem = oc::hal::teensy::SDFileSystemBackend;

#if defined(OC_HAL_TEENSY_SD_FILESYSTEM_SMOKE)
SDFileSystem filesystem;
#endif

void setup() {
#if defined(OC_HAL_TEENSY_SD_FILESYSTEM_SMOKE)
    Serial.begin(115200);
    const uint32_t deadline = millis() + 2000;
    while (!Serial && millis() < deadline) {}
    oc::hal::teensy::runSDFileSystemSmokeTest(filesystem, Serial);
#endif
}

void loop() {}
