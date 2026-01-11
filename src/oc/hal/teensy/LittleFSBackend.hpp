#pragma once

#include <LittleFS.h>

#include <oc/hal/IStorageBackend.hpp>

namespace oc::hal::teensy {

/**
 * @brief LittleFS storage backend for Teensy 4.x
 *
 * Uses LittleFS_Program (internal flash) with wear leveling.
 * Stores settings in a single file for address-based access.
 *
 * @note All interrupts are disabled during write/erase operations.
 * @note Uploading new code erases the filesystem.
 *
 * Usage:
 * @code
 * LittleFSBackend flash(1024 * 1024);  // 1MB filesystem
 * if (flash.begin()) {
 *     Settings<MySettings> settings(flash, 0x0000, 1);
 *     settings.load();
 * }
 * @endcode
 *
 * @see https://github.com/PaulStoffregen/LittleFS
 */
class LittleFSBackend : public hal::IStorageBackend {
public:
    /**
     * @brief Construct LittleFS backend
     * @param fsSize Filesystem size in bytes (min 65536, must fit in unused flash)
     * @param filename Storage file name (default: "settings.bin")
     */
    explicit LittleFSBackend(size_t fsSize = 512 * 1024,
                             const char* filename = "/settings.bin")
        : fsSize_(fsSize), filename_(filename) {}

    /**
     * @brief Initialize the filesystem
     * @return true if filesystem mounted successfully
     */
    bool begin() {
        if (initialized_) return true;

        if (!fs_.begin(fsSize_)) {
            return false;
        }

        initialized_ = true;
        return true;
    }

    bool available() const override {
        return initialized_;
    }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!initialized_ || address + size > capacity_) return 0;

        File file = fs_.open(filename_, FILE_READ);
        if (!file) {
            // File doesn't exist yet - return zeros (fresh state)
            memset(buffer, 0xFF, size);
            return size;
        }

        if (!file.seek(address)) {
            file.close();
            return 0;
        }

        size_t bytesRead = file.read(buffer, size);
        file.close();

        // Pad with 0xFF if file is shorter than requested
        if (bytesRead < size) {
            memset(buffer + bytesRead, 0xFF, size - bytesRead);
        }

        return size;
    }

    size_t write(uint32_t address, const uint8_t* buffer, size_t size) override {
        if (!initialized_ || address + size > capacity_) return 0;

        // Read existing content to preserve data outside write region
        File file = fs_.open(filename_, FILE_READ);
        size_t existingSize = file ? file.size() : 0;

        // Create temporary buffer for the region we need
        // Note: For large files, consider chunked approach
        uint8_t* temp = nullptr;
        if (existingSize > 0 && address > 0) {
            temp = new uint8_t[address];
            if (temp && file) {
                file.read(temp, address);
            }
        }
        if (file) file.close();

        // Write new content
        file = fs_.open(filename_, FILE_WRITE);
        if (!file) {
            delete[] temp;
            return 0;
        }

        // Write preserved prefix if any
        if (temp && address > 0) {
            file.write(temp, address);
            delete[] temp;
        } else if (address > 0) {
            // Pad with 0xFF to reach address
            for (size_t i = 0; i < address; ++i) {
                file.write(static_cast<uint8_t>(0xFF));
            }
        }

        // Write new data
        size_t written = file.write(buffer, size);
        file.close();

        return written;
    }

    bool commit() override {
        // LittleFS handles this internally
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (!initialized_) return false;

        // Write 0xFF (erased state) to the region
        uint8_t* eraseBuffer = new uint8_t[size];
        memset(eraseBuffer, 0xFF, size);
        size_t result = write(address, eraseBuffer, size);
        delete[] eraseBuffer;

        return result == size;
    }

    size_t capacity() const override {
        return capacity_;
    }

    /**
     * @brief Set the virtual capacity for settings storage
     * @param cap Maximum address space (default 4KB like EEPROM)
     */
    void setCapacity(size_t cap) {
        capacity_ = cap;
    }

    /**
     * @brief Get actual filesystem usage
     */
    size_t usedSpace() {
        if (!initialized_) return 0;
        return fsSize_ - fs_.totalSize() + fs_.usedSize();
    }

    /**
     * @brief Format the filesystem (erases all data)
     */
    bool format() {
        if (!initialized_) return false;
        fs_.quickFormat();
        return true;
    }

private:
    LittleFS_Program fs_;
    size_t fsSize_;
    const char* filename_;
    size_t capacity_ = 4096;  // Default 4KB like EEPROM
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
