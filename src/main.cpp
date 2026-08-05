/**
 * @file main.cpp
 * @brief Compilation test for driver-teensy
 */
#include <Arduino.h>

#include <cstring>

#include <oc/hal/teensy/EEPROMBackend.hpp>
#include <oc/hal/teensy/EncoderController.hpp>
#include <oc/hal/teensy/Ili9341.hpp>
#include <oc/hal/teensy/LittleFSBackend.hpp>
#include <oc/hal/teensy/SDCardBackend.hpp>
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
EXTMEM alignas(4) uint8_t psramWritePayload[32552];
#endif

void setup() {
#if defined(OC_HAL_TEENSY_SD_FILESYSTEM_SMOKE)
    Serial.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!Serial && millis() < deadline) {}

    // Exercise the product topology: an IStorage handle remains open while the
    // filesystem backend shares the same Teensy SD/SdFat singleton.
    oc::hal::teensy::SDCardBackend settings("/oc-fs-smoke-settings.bin", 4096);
    const auto settingsInit = settings.init();
    const auto filesystemInit = filesystem.init();
    constexpr uint8_t settingsPayload[] = {0x4F, 0x43, 0x53, 0x44};
    uint8_t settingsReadback[sizeof(settingsPayload)] = {};
    const bool settingsReady = settingsInit && filesystemInit &&
        settings.write(0, settingsPayload, sizeof(settingsPayload)) ==
            sizeof(settingsPayload) &&
        settings.commit() &&
        settings.read(0, settingsReadback, sizeof(settingsReadback)) ==
            sizeof(settingsReadback) &&
        std::memcmp(settingsPayload, settingsReadback, sizeof(settingsPayload)) == 0;
    Serial.println(settingsReady
        ? "[oc-fs-smoke] shared settings handle ready"
        : "[oc-fs-smoke] shared settings handle failed");
    if (settingsReady) {
        const bool smallSmokeOk =
            oc::hal::teensy::runSDFileSystemSmokeTest(filesystem, Serial);
        for (size_t i = 0; i < sizeof(psramWritePayload); ++i) {
            psramWritePayload[i] = static_cast<uint8_t>((i * 37U) ^ (i >> 3U));
        }
        constexpr const char* psramPath = "/oc-fs-smoke-psram.bin";
        bool psramWriteOk = smallSmokeOk &&
            filesystem.beginWrite(psramPath, sizeof(psramWritePayload));
        for (size_t offset = 0; psramWriteOk && offset < sizeof(psramWritePayload);) {
            const size_t remaining = sizeof(psramWritePayload) - offset;
            const size_t chunk = remaining < 4096U ? remaining : 4096U;
            const auto appended = filesystem.appendWrite(psramWritePayload + offset, chunk);
            psramWriteOk = appended && appended.value() == chunk;
            offset += chunk;
        }
        if (psramWriteOk) {
            psramWriteOk = static_cast<bool>(filesystem.finishWrite());
        } else {
            filesystem.abortWrite();
        }

        bool psramReadOk = psramWriteOk;
        if (psramReadOk) {
            std::memset(psramWritePayload, 0, sizeof(psramWritePayload));
            for (size_t offset = 0; psramReadOk && offset < sizeof(psramWritePayload);) {
                const size_t remaining = sizeof(psramWritePayload) - offset;
                const size_t chunk = remaining < 4096U ? remaining : 4096U;
                const auto read = filesystem.read(
                    psramPath,
                    offset,
                    psramWritePayload + offset,
                    chunk
                );
                psramReadOk = read && read.value() == chunk;
                offset += chunk;
            }
        }
        for (size_t i = 0; psramReadOk && i < sizeof(psramWritePayload); ++i) {
            const uint8_t expected = static_cast<uint8_t>((i * 37U) ^ (i >> 3U));
            psramReadOk = psramWritePayload[i] == expected;
        }
        Serial.println(psramWriteOk && psramReadOk
            ? "[oc-fs-smoke] PSRAM 32552B/4096B read/write session OK"
            : "[oc-fs-smoke] PSRAM 32552B/4096B read/write session FAIL");
        (void)filesystem.remove(psramPath);
    }
#endif
}

void loop() {}
