#include "Ili9341.hpp"

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
