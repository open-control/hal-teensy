#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <ILI9341_T4.h>

#include <oc/core/Result.hpp>
#include <oc/hal/IDisplayDriver.hpp>

namespace oc::teensy {

/**
 * @brief Hardware configuration for ILI9341 display (constexpr-friendly)
 *
 * All fields have sensible defaults for Teensy 4.1.
 * This struct contains only compile-time constants, no runtime pointers.
 */
struct Ili9341Config {
    // ── Dimensions ──
    uint16_t width  = 320;
    uint16_t height = 240;

    // ── SPI Pins (Teensy 4.1 defaults) ──
    uint16_t csPin   = 28;
    uint16_t dcPin   = 0;
    uint16_t rstPin  = 29;
    uint16_t mosiPin = 26;
    uint16_t sckPin  = 27;
    uint16_t misoPin = 1;
    uint32_t spiSpeed = 40'000'000;  ///< 40 MHz recommended

    // ── Display ──
    uint16_t rotation = 3;            ///< 0-3, landscape USB right = 3
    bool invertDisplay = true;       ///< Color inversion

    // ── DMA tuning ──
    uint16_t vsyncSpacing = 1;        ///< Frames between updates
    uint16_t diffGap = 6;             ///< Diff algorithm gap
    uint16_t irqPriority = 128;       ///< DMA IRQ priority
    float lateStartRatio = 0.3f;     ///< Late start optimization
    uint32_t refreshRate = 60;       ///< Target refresh Hz

    /// Calculate framebuffer size in pixels
    constexpr size_t framebufferSize() const { return width * height; }

    /// Recommended diff buffer size for this resolution
    constexpr size_t recommendedDiffSize() const {
        // ~8KB for 320x240, scales with resolution
        return (width * height) / 10;
    }
};

/**
 * @brief Runtime buffers for ILI9341 display
 *
 * All buffers must be in DMAMEM on Teensy 4.x.
 * Sizes are auto-calculated from config if set to 0.
 */
struct Ili9341Buffers {
    uint16_t* framebuffer = nullptr;  ///< Required: DMAMEM uint16_t[width*height]
    uint8_t* diff1 = nullptr;         ///< Required: DMAMEM for diff algorithm
    uint8_t* diff2 = nullptr;         ///< Optional: enables double-buffered diff
    size_t diff1Size = 0;             ///< 0 = auto-calculate from config
    size_t diff2Size = 0;             ///< 0 = auto-calculate from config
};

/**
 * @brief ILI9341 display driver for Teensy 4.x
 *
 * Uses ILI9341_T4 library with DMA for high-performance rendering.
 *
 * @code
 * // Config.hpp
 * constexpr Ili9341Config DISPLAY_CONFIG = {
 *     .width = 320, .height = 240,
 *     .csPin = 28, .dcPin = 0, ...
 * };
 *
 * // main.cpp
 * display.emplace(DISPLAY_CONFIG, {
 *     .framebuffer = Buffers::fb,
 *     .diff1 = Buffers::diff1,
 *     .diff2 = Buffers::diff2
 * });
 * display->init();
 * @endcode
 */
class Ili9341 : public hal::IDisplayDriver {
public:
    Ili9341(const Ili9341Config& config, const Ili9341Buffers& buffers);
    ~Ili9341() override = default;

    // Moveable
    Ili9341(Ili9341&&) noexcept = default;
    Ili9341& operator=(Ili9341&&) noexcept = default;

    // Non-copyable
    Ili9341(const Ili9341&) = delete;
    Ili9341& operator=(const Ili9341&) = delete;

    core::Result<void> init() override;
    void flush(const void* buffer, const hal::Rect& area) override;
    uint16_t width() const override { return config_.width; }
    uint16_t height() const override { return config_.height; }

    void waitAsyncComplete();

private:
    Ili9341Config config_;
    Ili9341Buffers buffers_;
    size_t effectiveDiff1Size_;
    size_t effectiveDiff2Size_;
    std::optional<ILI9341_T4::ILI9341Driver> tft_;
    std::unique_ptr<ILI9341_T4::DiffBuff> diff1_;
    std::unique_ptr<ILI9341_T4::DiffBuff> diff2_;
    bool initialized_ = false;
};

}  // namespace oc::teensy
