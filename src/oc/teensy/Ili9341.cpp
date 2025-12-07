#include "Ili9341.hpp"

namespace oc::teensy {

Ili9341::Ili9341(const Ili9341Config& config, const Ili9341Buffers& buffers)
    : config_(config)
    , buffers_(buffers)
    , effectiveDiff1Size_(buffers.diff1Size > 0 ? buffers.diff1Size : config.recommendedDiffSize())
    , effectiveDiff2Size_(buffers.diff2Size > 0 ? buffers.diff2Size : config.recommendedDiffSize())
{}

bool Ili9341::init() {
    if (initialized_) return true;

    if (!buffers_.framebuffer) return false;
    if (!buffers_.diff1) return false;

    diff1_ = std::make_unique<ILI9341_T4::DiffBuff>(buffers_.diff1, effectiveDiff1Size_);
    if (buffers_.diff2) {
        diff2_ = std::make_unique<ILI9341_T4::DiffBuff>(buffers_.diff2, effectiveDiff2Size_);
    }

    tft_.emplace(config_.csPin, config_.dcPin, config_.sckPin,
                 config_.mosiPin, config_.misoPin, config_.rstPin);

    if (!tft_->begin(config_.spiSpeed)) return false;

    tft_->setRotation(config_.rotation);
    tft_->invertDisplay(config_.invertDisplay);
    tft_->setFramebuffer(buffers_.framebuffer);

    if (diff2_) {
        tft_->setDiffBuffers(diff1_.get(), diff2_.get());
    } else {
        tft_->setDiffBuffers(diff1_.get());
    }

    tft_->setVSyncSpacing(config_.vsyncSpacing);
    tft_->setDiffGap(config_.diffGap);
    tft_->setIRQPriority(config_.irqPriority);
    tft_->setLateStartRatio(config_.lateStartRatio);
    tft_->clear(0x0000);

    initialized_ = true;
    return true;
}

void Ili9341::flush(const void* buffer, const hal::Rect& /*area*/) {
    if (!initialized_) return;

    // Async update - false = don't wait for redraw
    tft_->update(reinterpret_cast<uint16_t*>(const_cast<void*>(buffer)), false);
}

void Ili9341::waitAsyncComplete() {
    if (tft_) tft_->waitUpdateAsyncComplete();
}

}  // namespace oc::teensy
