/**
 * @file main.cpp
 * @brief Compilation test for driver-teensy
 */
#include <Arduino.h>

#include <oc/teensy/EEPROMBackend.hpp>
#include <oc/teensy/EncoderController.hpp>
#include <oc/teensy/Ili9341.hpp>
#include <oc/teensy/LittleFSBackend.hpp>
#include <oc/teensy/UsbMidi.hpp>

// Verify types compile correctly
using Midi = oc::teensy::UsbMidi;
using EepromStorage = oc::teensy::EEPROMBackend;
using FlashStorage = oc::teensy::LittleFSBackend;

void setup() {}
void loop() {}
