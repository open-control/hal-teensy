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
 * #include <oc/hal/teensy/TeensyOutput.hpp>
 *
 * void setup() {
 *     oc::log::setOutput(oc::hal::teensy::serialOutput());
 *     OC_LOG_INFO("Boot started");
 * }
 * @endcode
 */

#include <Arduino.h>
#include <new>
#include <oc/log/Log.hpp>
#include "detail/SerialLogBuffer.hpp"

namespace oc::hal::teensy {

namespace detail {
class SerialLogPrint final : public Print {
public:
    size_t write(uint8_t byte) override {
        // Formatting in an ISR must not corrupt an in-progress foreground line
        // or enter the USB SDK. Count rejected ISR lines at their terminator.
        uint32_t ipsr = 0;
        asm volatile("MRS %0, ipsr" : "=r"(ipsr));
        if (ipsr != 0U) {
            if (byte == '\n') buffer_.dropLine();
            return 1;
        }
        buffer_.append(byte, byte == '\n' ? millis() : 0U,
            [](const uint8_t* data, size_t size) {
                // SDK availableForWrite is the non-waiting admission contract.
                return tryWriteSerialLog(Serial, usb_configuration != 0, data, size);
            });
        return 1;
    }
private:
    SerialLogBuffer buffer_;
};

inline SerialLogPrint& serialLogPrint() {
    alignas(SerialLogPrint) DMAMEM static uint8_t storage[sizeof(SerialLogPrint)];
    static auto* output = new (storage) SerialLogPrint;
    return *output;
}
} // namespace detail

/**
 * @brief Get the Serial-based log output for Teensy
 *
 * Returns a static, non-waiting USB Serial line output. Lines longer than
 * 512 bytes or not admitted by the SDK are dropped and counted. ISR logging
 * is also dropped; output resumes with a loss summary once the host drains.
 *
 * @return Reference to the Teensy Serial output implementation
 */
inline const oc::log::Output& serialOutput() {
    static const oc::log::Output output = {
        // printChar
        [](char c) { detail::serialLogPrint().print(c); },
        // printStr
        [](const char* str) { detail::serialLogPrint().print(str); },
        // printInt32
        [](int32_t value) { detail::serialLogPrint().print(value); },
        // printUint32
        [](uint32_t value) { detail::serialLogPrint().print(value); },
        // printFloat
        [](float value) { detail::serialLogPrint().print(value, 4); },
        // printBool
        [](bool value) { detail::serialLogPrint().print(value ? "true" : "false"); },
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
 * @param timeoutMs Maximum time to wait in milliseconds (default 5000)
 */
inline void waitForSerial(uint32_t timeoutMs = 5000) {
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
 * @param waitTimeoutMs Timeout for Serial wait (default 5000ms)
 *
 * @code
 * void setup() {
 *     oc::hal::teensy::initLogging();  // Wait + configure
 *     OC_LOG_INFO("Boot started");
 * }
 * @endcode
 */
inline void initLogging(uint32_t waitTimeoutMs = 5000) {
    waitForSerial(waitTimeoutMs);
    oc::log::setOutput(serialOutput());
}

}  // namespace oc::hal::teensy
