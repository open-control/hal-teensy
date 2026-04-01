#pragma once

#include <cstdint>

#include <Arduino.h>

namespace oc::hal::teensy {

/**
 * @brief Monotonic microsecond clock backed by the Teensy cycle counter.
 *
 * This avoids relying on a periodic ISR to advance transport time. As long as
 * the caller samples at least once every ~7 seconds (32-bit cycle wrap on
 * Teensy 4.x), `micros64()` stays monotonic even if interrupts are delayed.
 */
class HighResolutionClock {
public:
    uint64_t micros64();

private:
    uint32_t last_cycles_ = 0;
    uint64_t cycle_wrap_base_ = 0;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
