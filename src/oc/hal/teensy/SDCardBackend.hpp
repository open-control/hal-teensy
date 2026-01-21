#pragma once

#include <SD.h>
#include <cstring>

#include <oc/interface/IStorage.hpp>
#include <oc/log/Log.hpp>
#include <oc/types/Result.hpp>

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
 * File handle is kept open for fast read/write access.
 * commit() flushes data to ensure persistence.
 *
 * Usage:
 * @code
 * SDCardBackend storage("/settings.bin");
 * if (!storage.begin()) {
 *     // SD card not inserted or failed
 * }
 *
 * storage.write(0x0000, data, size);  // Direct write via open handle
 * storage.commit();                    // Flush to SD
 * @endcode
 *
 * @note Requires micro SD card in Teensy 4.1 built-in slot
 */
class SDCardBackend : public interface::IStorage {
public:
    /**
     * @brief Construct SD card backend
     * @param filename File path on SD card (e.g., "/settings.bin")
     * @param capacity Max addressable size (guard against wild addresses)
     */
    explicit SDCardBackend(const char* filename = "/settings.bin",
                           size_t capacity = 1024 * 1024)
        : filename_(filename), capacity_(capacity) {}

    ~SDCardBackend() {
        if (file_) {
            file_.close();
        }
    }

    /**
     * @brief Initialize SD card and open file handle
     * @return Result<void> - ok() if SD card mounted and file opened
     */
    oc::Result<void> init() override {
        if (initialized_) return oc::Result<void>::ok();

        if (!SD.begin(BUILTIN_SDCARD)) {
            OC_LOG_ERROR("[SDCard] SD.begin() failed");
            return oc::Result<void>::err({oc::ErrorCode::HARDWARE_INIT_FAILED, "SD.begin() failed"});
        }

        // Open with read+write, create if needed
        file_ = SD.open(filename_, FILE_WRITE);
        if (!file_) {
            OC_LOG_ERROR("[SDCard] Failed to open {}", filename_);
            return oc::Result<void>::err({oc::ErrorCode::HARDWARE_INIT_FAILED, "Failed to open file"});
        }

        initialized_ = true;
        return oc::Result<void>::ok();
    }

    bool available() const override {
        return initialized_ && SD.mediaPresent();
    }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!file_ || address + size > capacity_) return 0;

        size_t fileSize = file_.size();
        if (address >= fileSize) {
            std::memset(buffer, 0xFF, size);
            return size;
        }

        file_.seek(address);
        size_t bytesRead = file_.read(buffer, size);

        // Pad with 0xFF if file is shorter than requested
        if (bytesRead < size) {
            std::memset(buffer + bytesRead, 0xFF, size - bytesRead);
        }

        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!file_ || address + size > capacity_) return 0;

        // Pad with 0xFF if writing beyond current file size
        size_t fileSize = file_.size();
        if (address > fileSize) {
            file_.seek(fileSize);
            size_t gap = address - fileSize;
            while (gap > 0) {
                size_t chunk = (gap > sizeof(PADDING)) ? sizeof(PADDING) : gap;
                file_.write(PADDING, chunk);
                gap -= chunk;
            }
        }

        file_.seek(address);
        return file_.write(buffer, size);
    }

    bool commit() override {
        if (!file_) return false;
        file_.flush();
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (!file_ || address + size > capacity_) return false;

        file_.seek(address);
        size_t remaining = size;
        while (remaining > 0) {
            size_t chunk = (remaining > sizeof(PADDING)) ? sizeof(PADDING) : remaining;
            if (file_.write(PADDING, chunk) != chunk) {
                return false;
            }
            remaining -= chunk;
        }
        return true;
    }

    size_t capacity() const override {
        return capacity_;
    }

    bool isDirty() const override {
        return false;  // No tracking, caller should commit() when needed
    }

    /**
     * @brief Close and reopen file (for SD card hot-swap recovery)
     */
    bool reopen() {
        if (file_) file_.close();
        file_ = SD.open(filename_, FILE_WRITE);
        return static_cast<bool>(file_);
    }

private:
    static constexpr uint8_t PADDING[64] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    const char* filename_;
    size_t capacity_;
    File file_;  // Persistent handle - avoids open/close overhead
    bool initialized_ = false;
};

// Static member definition
constexpr uint8_t SDCardBackend::PADDING[64];

}  // namespace oc::hal::teensy
