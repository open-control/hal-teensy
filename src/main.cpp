/**
 * @file main.cpp
 * @brief Compilation test for driver-teensy
 */
#include <Arduino.h>

#include <oc/hal/teensy/EEPROMBackend.hpp>
#include <oc/hal/teensy/EncoderController.hpp>
#include <oc/hal/teensy/Ili9341.hpp>
#include <oc/hal/teensy/LittleFSBackend.hpp>
#include <oc/hal/teensy/UsbMidi.hpp>

// Verify types compile correctly
using Midi = oc::hal::teensy::UsbMidi;
using EepromStorage = oc::hal::teensy::EEPROMBackend;
using FlashStorage = oc::hal::teensy::LittleFSBackend;

void setup() {}
void loop() {}
