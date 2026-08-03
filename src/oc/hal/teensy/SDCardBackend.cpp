#include "SDCardBackend.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "BuiltInSD.hpp"

namespace oc::hal::teensy {

const uint8_t SDCardBackend::PADDING[64] PROGMEM = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

SDCardBackend::~SDCardBackend() {
    if (file_.isOpen()) file_.close();
}

FLASHMEM oc::type::Result<void> SDCardBackend::init() {
    if (initialized_) {
        if (available()) return oc::type::Result<void>::ok();
        if (reopen()) return oc::type::Result<void>::ok();
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "SD reopen failed"}
        );
    }

    if (!detail::initializeBuiltInSD()) {
        OC_LOG_ERROR("[SDCard] SDIO DMA init failed");
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "SDIO DMA init failed"}
        );
    }

    // Arduino's File wrapper allocates an SDFile on the heap and FILE_WRITE
    // adds O_AT_END, which defeats addressed settings writes after seek().
    if (!file_.open(filename_, O_RDWR | O_CREAT)) {
        OC_LOG_ERROR("[SDCard] Failed to open {}", filename_);
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "Failed to open file"}
        );
    }

    initialized_ = true;
    return oc::type::Result<void>::ok();
}

FLASHMEM bool SDCardBackend::available() const {
    return initialized_ && detail::builtInSDMediaPresent();
}

FLASHMEM size_t SDCardBackend::read(uint32_t address, uint8_t* buffer, size_t size) {
    if (!file_.isOpen() || address > capacity_ || size > capacity_ - address) return 0;

    const uint64_t fileSize = file_.fileSize();
    if (address >= fileSize) {
        std::memset(buffer, 0xFF, size);
        return size;
    }

    if (!file_.seekSet(address)) return 0;
    const size_t readable = static_cast<size_t>(
        (fileSize - address) < size ? (fileSize - address) : size
    );
    const int bytesReadResult = file_.read(buffer, readable);
    if (bytesReadResult < 0) return 0;
    const size_t bytesRead = static_cast<size_t>(bytesReadResult);

    if (bytesRead < size) {
        std::memset(buffer + bytesRead, 0xFF, size - bytesRead);
    }
    return size;
}

FLASHMEM size_t SDCardBackend::write(
    uint32_t address,
    const uint8_t* buffer,
    size_t size
) {
    if (!file_.isOpen() || address > capacity_ || size > capacity_ - address) return 0;

    const uint64_t fileSize = file_.fileSize();
    if (address > fileSize) {
        if (!file_.seekSet(fileSize)) return 0;
        size_t gap = static_cast<size_t>(address - fileSize);
        while (gap > 0) {
            const size_t chunk = gap > sizeof(PADDING) ? sizeof(PADDING) : gap;
            if (file_.write(PADDING, chunk) != chunk) return 0;
            gap -= chunk;
        }
    }

    if (!file_.seekSet(address)) return 0;
    return file_.write(buffer, size);
}

FLASHMEM bool SDCardBackend::commit() {
    return file_.isOpen() && file_.sync();
}

FLASHMEM bool SDCardBackend::erase(uint32_t address, size_t size) {
    if (!file_.isOpen() || address > capacity_ || size > capacity_ - address) return false;
    if (!file_.seekSet(address)) return false;

    size_t remaining = size;
    while (remaining > 0) {
        const size_t chunk = remaining > sizeof(PADDING) ? sizeof(PADDING) : remaining;
        if (file_.write(PADDING, chunk) != chunk) return false;
        remaining -= chunk;
    }
    return true;
}

FLASHMEM bool SDCardBackend::reopen() {
    if (file_.isOpen()) file_.close();
    initialized_ = false;
    if (!detail::reinitializeBuiltInSD()) return false;
    initialized_ = file_.open(filename_, O_RDWR | O_CREAT);
    return initialized_;
}

}  // namespace oc::hal::teensy
