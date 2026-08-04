#include "BuiltInSD.hpp"

#include <SD.h>

namespace oc::hal::teensy::detail {
namespace {

bool initialized = false;
bool cachedPresent = false;
uint8_t consecutiveProbeFailures = 0;
uint32_t nextPresenceProbeMs = 0;

constexpr uint32_t INITIAL_PRESENCE_GRACE_MS = 1000U;
constexpr uint32_t PRESENCE_PROBE_INTERVAL_MS = 250U;
constexpr uint8_t PRESENCE_FAILURE_THRESHOLD = 3U;

bool deadlinePending(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) < 0;
}

FLASHMEM bool configureBuiltInSD() {
    // Mount the shared SdFat volume exactly once in DMA mode. Calling
    // SD.begin(BUILTIN_SDCARD) first mounts the same object in FIFO mode and a
    // second begin() leaves the SDHC/card state vulnerable to later timeouts.
    FsDateTime::setCallback(SDClass::dateTime);
    if (!SD.sdfs.begin(SdioConfig(DMA_SDIO))) return false;
    initialized = true;
    cachedPresent = true;
    consecutiveProbeFailures = 0;
    nextPresenceProbeMs = millis() + INITIAL_PRESENCE_GRACE_MS;
    return true;
}

}  // namespace

FLASHMEM bool initializeBuiltInSD() {
    return initialized || configureBuiltInSD();
}

FLASHMEM bool reinitializeBuiltInSD() {
    cachedPresent = false;
    consecutiveProbeFailures = 0;
    nextPresenceProbeMs = 0;
    if (!initialized) return configureBuiltInSD();

    initialized = SD.sdfs.restart();
    if (!initialized) return false;
    cachedPresent = true;
    nextPresenceProbeMs = millis() + INITIAL_PRESENCE_GRACE_MS;
    return true;
}

FLASHMEM bool builtInSDMediaPresent() {
    if (!initialized) return false;

    const uint32_t now = millis();
    if (deadlinePending(now, nextPresenceProbeMs)) return cachedPresent;

    alignas(4) uint8_t probeSector[512];
    auto* card = SD.sdfs.card();
    const bool present = card != nullptr && card->readSector(0U, probeSector);
    nextPresenceProbeMs = millis() + PRESENCE_PROBE_INTERVAL_MS;
    if (present) {
        cachedPresent = true;
        consecutiveProbeFailures = 0;
        return true;
    }

    if (consecutiveProbeFailures < PRESENCE_FAILURE_THRESHOLD) {
        ++consecutiveProbeFailures;
    }
    if (consecutiveProbeFailures >= PRESENCE_FAILURE_THRESHOLD) {
        cachedPresent = false;
    }
    return cachedPresent;
}

}  // namespace oc::hal::teensy::detail
