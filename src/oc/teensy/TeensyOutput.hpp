#pragma once

/**
 * @file TeensyOutput.hpp
 * @brief Teensy-specific log output using Serial
 *
 * Provides the oc::log::Output implementation for Teensy boards.
 * Uses Arduino Serial for output.
 *
 * Usage in main.cpp:
 * @code
 * #include <oc/teensy/TeensyOutput.hpp>
 *
 * void setup() {
 *     oc::log::setOutput(oc::teensy::serialOutput());
 *     OC_LOG_INFO("Boot started");
 * }
 * @endcode
 */

#include <Arduino.h>
#include <oc/log/Log.hpp>

namespace oc::teensy {

/**
 * @brief Get the Serial-based log output for Teensy
 *
 * Returns a reference to a static Output instance configured
 * for Arduino Serial output.
 *
 * @return Reference to the Teensy Serial output implementation
 */
inline const oc::log::Output& serialOutput() {
    static const oc::log::Output output = {
        // printChar
        [](char c) { Serial.print(c); },
        // printStr
        [](const char* str) { Serial.print(str); },
        // printInt32
        [](int32_t value) { Serial.print(value); },
        // printUint32
        [](uint32_t value) { Serial.print(value); },
        // printFloat
        [](float value) { Serial.print(value, 4); },
        // printBool
        [](bool value) { Serial.print(value ? "true" : "false"); },
        // getTimeMs
        []() -> uint32_t { return millis(); }
    };
    return output;
}

/**
 * @brief Wait for Serial connection with timeout
 *
 * Blocks until USB Serial is connected or timeout expires.
 * Useful for ensuring boot logs are visible.
 *
 * @param timeoutMs Maximum time to wait in milliseconds (default 3000)
 */
inline void waitForSerial(uint32_t timeoutMs = 3000) {
    while (!Serial && millis() < timeoutMs) {
        // Wait for USB Serial connection
    }
}

/**
 * @brief Initialize logging for Teensy
 *
 * Convenience function that waits for Serial and configures log output.
 * Call this at the start of setup().
 *
 * @param waitTimeoutMs Timeout for Serial wait (default 3000ms)
 *
 * @code
 * void setup() {
 *     oc::teensy::initLogging();  // Wait + configure
 *     OC_LOG_INFO("Boot started");
 * }
 * @endcode
 */
inline void initLogging(uint32_t waitTimeoutMs = 3000) {
    waitForSerial(waitTimeoutMs);
    oc::log::setOutput(serialOutput());
}

}  // namespace oc::teensy
