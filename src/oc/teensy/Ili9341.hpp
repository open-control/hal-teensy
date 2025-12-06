#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <ILI9341_T4.h>

#include <oc/hal/IDisplayDriver.hpp>

namespace oc::teensy {

/**
 * ILI9341 display configuration.
 * 
 * Only framebuffer and diffBuffers are required.
 * All other fields have sensible defaults for Teensy 4.x.
 */
struct Ili9341Config {
    // ── Dimensions ──
    uint16_t width  = 320;
    uint16_t height = 240;

    // ── SPI Pins (Teensy 4.1 defaults) ──
    uint8_t csPin   = 28;
    uint8_t dcPin   = 0;
    uint8_t rstPin  = 29;
    uint8_t mosiPin = 26;
    uint8_t sckPin  = 27;
    uint8_t misoPin = 1;
    uint32_t spiSpeed = 40'000'000;  ///< 40 MHz recommended

    // ── Display ──
    uint8_t rotation = 3;            ///< 0-3, landscape USB right = 3
    bool invertDisplay = true;       ///< Color inversion

    // ── DMA tuning ──
    uint8_t vsyncSpacing = 1;        ///< Frames between updates
    uint8_t diffGap = 6;             ///< Diff algorithm gap
    uint8_t irqPriority = 128;       ///< DMA IRQ priority
    float lateStartRatio = 0.3f;     ///< Late start optimization
    uint8_t refreshRate = 60;        ///< Target refresh Hz

    // ── Buffers (REQUIRED, use DMAMEM) ──
    uint16_t* framebuffer = nullptr;
    uint8_t* diffBuffer1 = nullptr;
    size_t diffBuffer1Size = 0;
    uint8_t* diffBuffer2 = nullptr;
    size_t diffBuffer2Size = 0;
};

class Ili9341 : public hal::IDisplayDriver {
public:
    explicit Ili9341(const Ili9341Config& config);
    ~Ili9341() override = default;

    bool init() override;
    void flush(const void* buffer, const hal::Rect& area) override;
    uint16_t width() const override { return config_.width; }
    uint16_t height() const override { return config_.height; }

    void waitAsyncComplete();

private:
    Ili9341Config config_;
    std::optional<ILI9341_T4::ILI9341Driver> tft_;
    std::unique_ptr<ILI9341_T4::DiffBuff> diff1_;
    std::unique_ptr<ILI9341_T4::DiffBuff> diff2_;
    bool initialized_ = false;
};

}  // namespace oc::teensy
