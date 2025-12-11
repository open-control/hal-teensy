#pragma once

#include <EEPROM.h>

#include <oc/hal/IStorageBackend.hpp>

namespace oc::teensy {

/**
 * @brief EEPROM storage backend for Teensy
 *
 * Teensy 4.x has 4KB of emulated EEPROM.
 * Writes are immediate (no buffering needed).
 *
 * Usage:
 * @code
 * EEPROMBackend eeprom;
 * Settings<MySettings> settings(eeprom, 0x0000, 1);
 *
 * settings.load();
 * settings.modify([](auto& s) { s.volume = 0.75f; });
 * settings.save();
 * @endcode
 */
class EEPROMBackend : public hal::IStorageBackend {
public:
    bool available() const override {
        return true;
    }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (address + size > capacity()) return 0;
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = EEPROM.read(address + i);
        }
        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (address + size > capacity()) return 0;
        for (size_t i = 0; i < size; ++i) {
            EEPROM.update(address + i, buffer[i]);  // Smart write (no-op if unchanged)
        }
        return size;
    }

    bool commit() override {
        return true;  // EEPROM writes are immediate on Teensy
    }

    bool erase(uint32_t address, size_t size) override {
        if (address + size > capacity()) return false;
        for (size_t i = 0; i < size; ++i) {
            EEPROM.update(address + i, 0xFF);
        }
        return true;
    }

    size_t capacity() const override {
        return EEPROM.length();  // 4096 on Teensy 4.x
    }
};

}  // namespace oc::teensy
