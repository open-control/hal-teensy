#pragma once

/**
 * @file LogOutput.hpp
 * @brief Teensy-specific log output via USB Serial
 *
 * Defines OC_LOG_PRINT for the framework's Log API.
 * Must be included BEFORE oc/log/Log.hpp (done automatically via Teensy.hpp).
 */

#include <Arduino.h>

// Serial.print handles all basic types: int, float, const char*, String, etc.
#define OC_LOG_PRINT(x) Serial.print(x)
