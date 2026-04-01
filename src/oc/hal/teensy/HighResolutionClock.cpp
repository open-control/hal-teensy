#include "HighResolutionClock.hpp"

namespace oc::hal::teensy {

uint64_t HighResolutionClock::micros64() {
    const uint32_t cycles = ARM_DWT_CYCCNT;

    if (!initialized_) {
        initialized_ = true;
        last_cycles_ = cycles;
    } else if (cycles < last_cycles_) {
        cycle_wrap_base_ += (1ULL << 32);
    }

    last_cycles_ = cycles;

    const uint64_t totalCycles = cycle_wrap_base_ | static_cast<uint64_t>(cycles);
    return (totalCycles * 1000000ULL) / static_cast<uint64_t>(F_CPU_ACTUAL);
}

}  // namespace oc::hal::teensy
