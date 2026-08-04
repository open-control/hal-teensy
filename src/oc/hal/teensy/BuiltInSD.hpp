#pragma once

#include <config/PlatformCompat.hpp>

namespace oc::hal::teensy::detail {

/**
 * Configure the Teensy 4.1 built-in slot exactly once before any FsFile is
 * opened. The shared SdFat volume is mounted directly in DMA mode so display
 * interrupts cannot starve SDIO FIFO transfers and the card is not mounted a
 * second time with conflicting driver state.
 */
FLASHMEM bool initializeBuiltInSD();

FLASHMEM bool reinitializeBuiltInSD();

FLASHMEM bool builtInSDMediaPresent();

}  // namespace oc::hal::teensy::detail
