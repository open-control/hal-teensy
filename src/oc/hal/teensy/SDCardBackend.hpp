#pragma once

#include <SD.h>

#include <oc/interface/IStorage.hpp>
#include <oc/type/Result.hpp>

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

    ~SDCardBackend() override;

    /**
     * @brief Initialize SD card and open file handle
     * @return Result<void> - ok() if SD card mounted and file opened
     */
    oc::type::Result<void> init() override;

    bool available() const override;

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override;

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override;

    bool commit() override;

    bool erase(uint32_t address, size_t size) override;

    size_t capacity() const override {
        return capacity_;
    }

    bool isDirty() const override {
        return false;  // No tracking, caller should commit() when needed
    }

    /**
     * @brief Close and reopen file (for SD card hot-swap recovery)
     */
    bool reopen();

private:
    static const uint8_t PADDING[64];

    const char* filename_;
    size_t capacity_;
    FsFile file_;  // Persistent, allocation-free SdFat handle
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
