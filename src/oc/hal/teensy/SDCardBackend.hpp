#pragma once

#include <SD.h>
#include <cstring>
#include <vector>

#include <oc/hal/IStorageBackend.hpp>
#include <oc/log/Log.hpp>

namespace oc::hal::teensy {

/**
 * @brief SD Card storage backend for Teensy 4.1
 *
 * Uses SDIO native interface which is on a separate bus from FlexSPI.
 * This means SD card operations don't block PROGMEM reads or interrupts.
 *
 * Architecture:
 * ```
 * CPU --> FlexSPI --> Flash NOR (PROGMEM, code, fonts)
 *     |
 *     --> SDIO ----> SD Card (storage)  <-- Separate bus!
 * ```
 *
 * Data is cached in RAM for instant read/write access.
 * Persistence happens only on commit() (~3ms write latency).
 *
 * Usage:
 * @code
 * SDCardBackend storage("/macros.bin");
 * if (!storage.begin()) {
 *     // SD card not inserted or failed
 * }
 *
 * storage.write(0x0000, data, size);  // Instant (RAM cache)
 * storage.commit();                    // ~3ms (writes to SD)
 * @endcode
 *
 * @note Requires micro SD card in Teensy 4.1 built-in slot
 * @note File is created on first commit() if it doesn't exist
 */
class SDCardBackend : public hal::IStorageBackend {
public:
    /**
     * @brief Construct SD card backend
     * @param filename File path on SD card (e.g., "/settings.bin")
     * @param capacity Virtual capacity in bytes (default 4KB like EEPROM)
     */
    explicit SDCardBackend(const char* filename = "/settings.bin",
                           size_t capacity = 4096)
        : filename_(filename), capacity_(capacity) {}

    /**
     * @brief Initialize SD card and load data to cache
     * @return true if SD card mounted and ready
     *
     * If the file doesn't exist, cache starts empty (filled with 0xFF on read).
     * If the file exists, its contents are loaded into RAM cache.
     */
    bool begin() override {
        if (initialized_) return true;

        if (!SD.begin(BUILTIN_SDCARD)) {
            OC_LOG_ERROR("[SDCard] SD.begin() failed");
            return false;
        }

        initialized_ = true;
        bool loaded = loadToCache();
        OC_LOG_INFO("[SDCard] Ready, cache={}B", cache_.size());
        return loaded;
    }

    bool available() const override {
        return initialized_;
    }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!initialized_ || address + size > capacity_) return 0;

        if (address + size <= cache_.size()) {
            // Fully within cache
            std::memcpy(buffer, cache_.data() + address, size);
        } else if (address < cache_.size()) {
            // Partially in cache, rest is 0xFF
            size_t fromCache = cache_.size() - address;
            std::memcpy(buffer, cache_.data() + address, fromCache);
            std::memset(buffer + fromCache, 0xFF, size - fromCache);
        } else {
            // Entirely beyond cache (unwritten region)
            std::memset(buffer, 0xFF, size);
        }
        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!initialized_ || address + size > capacity_) return 0;

        // Expand cache if needed
        if (address + size > cache_.size()) {
            size_t oldSize = cache_.size();
            cache_.resize(address + size);
            // Fill gap with 0xFF (erased state)
            if (address > oldSize) {
                std::memset(cache_.data() + oldSize, 0xFF, address - oldSize);
            }
        }

        std::memcpy(cache_.data() + address, buffer, size);
        dirty_ = true;
        return size;
    }

    bool commit() override {
        if (!dirty_ || !initialized_) return true;

        // FILE_WRITE = O_RDWR | O_CREAT | O_AT_END
        File file = SD.open(filename_, FILE_WRITE);
        if (!file) {
            OC_LOG_ERROR("[SDCard] Failed to open {}", filename_);
            return false;
        }

        // Seek to beginning since FILE_WRITE positions at end
        file.seek(0);
        size_t written = file.write(cache_.data(), cache_.size());

        // Truncate to written size (removes old data if file was larger)
        // NOTE: truncate() without arg = truncate(0) = erase all!
        file.truncate(written);
        file.close();

        if (written == cache_.size()) {
            dirty_ = false;
            return true;
        }
        OC_LOG_ERROR("[SDCard] Write failed ({}/{}B)", written, cache_.size());
        return false;
    }

    bool erase(uint32_t address, size_t size) override {
        if (!initialized_ || address + size > capacity_) return false;

        // Expand cache if needed to cover erase region
        if (address + size > cache_.size()) {
            cache_.resize(address + size, 0xFF);
        }

        std::memset(cache_.data() + address, 0xFF, size);
        dirty_ = true;
        return true;
    }

    size_t capacity() const override {
        return capacity_;
    }

    bool isDirty() const override {
        return dirty_;
    }

    /**
     * @brief Set the virtual capacity
     * @param cap Maximum addressable size in bytes
     */
    void setCapacity(size_t cap) {
        capacity_ = cap;
    }

    /**
     * @brief Get current cache size (actual data written)
     */
    size_t cacheSize() const {
        return cache_.size();
    }

private:
    bool loadToCache() {
        File file = SD.open(filename_, FILE_READ);
        if (!file) {
            // File doesn't exist yet - start with empty cache
            cache_.clear();
            return true;
        }

        size_t fileSize = file.size();
        if (fileSize > capacity_) {
            fileSize = capacity_;
        }

        cache_.resize(fileSize);
        file.read(cache_.data(), cache_.size());
        file.close();
        return true;
    }

    const char* filename_;
    size_t capacity_;
    bool initialized_ = false;
    bool dirty_ = false;
    std::vector<uint8_t> cache_;
};

}  // namespace oc::hal::teensy
