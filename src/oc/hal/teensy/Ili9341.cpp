#include "Ili9341.hpp"

namespace oc::hal::teensy {

Ili9341::Ili9341(const Ili9341Config& config, const Ili9341Buffers& buffers)
    : config_(config)
    , buffers_(buffers)
    , effectiveDiff1Size_(buffers.diff1Size > 0 ? buffers.diff1Size : config.recommendedDiffSize())
    , effectiveDiff2Size_(buffers.diff2Size > 0 ? buffers.diff2Size : config.recommendedDiffSize())
{}

oc::Result<void> Ili9341::init() {
    using R = oc::Result<void>;
    using E = oc::ErrorCode;

    if (initialized_) return R::ok();

    if (!buffers_.framebuffer) {
        return R::err({E::INVALID_ARGUMENT, "framebuffer required"});
    }
    if (!buffers_.diff1) {
        return R::err({E::INVALID_ARGUMENT, "diff1 buffer required"});
    }

    diff1_ = std::make_unique<ILI9341_T4::DiffBuff>(buffers_.diff1, effectiveDiff1Size_);
    if (buffers_.diff2) {
        diff2_ = std::make_unique<ILI9341_T4::DiffBuff>(buffers_.diff2, effectiveDiff2Size_);
    }

    tft_.emplace(config_.csPin, config_.dcPin, config_.sckPin,
                 config_.mosiPin, config_.misoPin, config_.rstPin);

    if (!tft_->begin(config_.spiSpeed)) {
        return R::err({E::HARDWARE_INIT_FAILED, "ILI9341 SPI begin failed"});
    }

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
    return R::ok();
}

void Ili9341::flush(const void* buffer, const interface::Rect& /*area*/) {
    if (!initialized_) return;

    // Async update - false = don't wait for redraw
    tft_->update(reinterpret_cast<uint16_t*>(const_cast<void*>(buffer)), false);
}

void Ili9341::waitAsyncComplete() {
    if (tft_) tft_->waitUpdateAsyncComplete();
}

}  // namespace oc::hal::teensy
