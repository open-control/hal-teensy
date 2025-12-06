#include "Ili9341.hpp"

#if __has_include(<ILI9341_T4.h>)

#include <cstring>

namespace oc::teensy {

Ili9341::Ili9341(const Ili9341Config& config) : config_(config) {}

bool Ili9341::init() {
    if (initialized_) return true;

    if (!config_.framebuffer) return false;
    if (!config_.diffBuffer1 || config_.diffBuffer1Size == 0) return false;

    diff1_ = std::make_unique<ILI9341_T4::DiffBuff>(config_.diffBuffer1, config_.diffBuffer1Size);
    if (config_.diffBuffer2 && config_.diffBuffer2Size > 0) {
        diff2_ = std::make_unique<ILI9341_T4::DiffBuff>(config_.diffBuffer2, config_.diffBuffer2Size);
    }

    tft_.emplace(config_.csPin, config_.dcPin, config_.sckPin,
                 config_.mosiPin, config_.misoPin, config_.rstPin);

    if (!tft_->begin(config_.spiSpeed)) return false;

    tft_->setRotation(config_.rotation);
    tft_->invertDisplay(config_.invertDisplay);
    tft_->setFramebuffer(config_.framebuffer);

    if (diff2_) {
        tft_->setDiffBuffers(diff1_.get(), diff2_.get());
    } else {
        tft_->setDiffBuffers(diff1_.get());
    }

    tft_->setVSyncSpacing(config_.vsyncSpacing);
    tft_->setDiffGap(config_.diffGap);
    tft_->setIRQPriority(config_.irqPriority);
    tft_->setLateStartRatio(config_.lateStartRatio);
    tft_->setRefreshRate(config_.refreshRate);
    tft_->clear(0x0000);

    initialized_ = true;
    return true;
}

void Ili9341::flush(const void* buffer, const hal::Rect& area) {
    if (!initialized_) return;

    const uint16_t* src = static_cast<const uint16_t*>(buffer);
    uint16_t area_width = area.x2 - area.x1 + 1;

    for (int16_t y = area.y1; y <= area.y2; ++y) {
        const uint16_t* src_row = src + (y - area.y1) * area_width;
        uint16_t* dst_row = config_.framebuffer + y * config_.width + area.x1;
        memcpy(dst_row, src_row, area_width * sizeof(uint16_t));
    }

    tft_->update(config_.framebuffer);
}

void Ili9341::waitAsyncComplete() {
    if (tft_) tft_->waitUpdateAsyncComplete();
}

}  // namespace oc::teensy

#endif
