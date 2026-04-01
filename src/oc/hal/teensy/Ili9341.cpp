#include "Ili9341.hpp"

namespace oc::hal::teensy {

namespace {

Ili9341::StatSummary toStatSummary(const ILI9341_T4::StatsVar& stats) {
    Ili9341::StatSummary summary;
    summary.count = stats.count();
    if (summary.count == 0) return summary;

    summary.min = stats.min();
    summary.max = stats.max();
    summary.avg = stats.avg();
    summary.stddev = stats.std();
    return summary;
}

Ili9341::DiffStats toDiffStats(const ILI9341_T4::DiffBuff& diff) {
    Ili9341::DiffStats stats;
    stats.computed = diff.statsNbComputed();
    stats.overflow = diff.statsNbOverflow();
    stats.overflowRatio = diff.statsOverflowRatio();
    stats.sizeBytes = toStatSummary(diff.statsSize());
    stats.computeTimeUs = toStatSummary(diff.statsTime());
    return stats;
}

}  // namespace

FLASHMEM Ili9341::Ili9341(const Ili9341Config& config, const Ili9341Buffers& buffers)
    : config_(config)
    , buffers_(buffers)
    , effectiveDiff1Size_(buffers.diff1Size > 0 ? buffers.diff1Size : config.recommendedDiffSize())
    , effectiveDiff2Size_(buffers.diff2Size > 0 ? buffers.diff2Size : config.recommendedDiffSize())
{}

FLASHMEM oc::type::Result<void> Ili9341::init() {
    using R = oc::type::Result<void>;
    using E = oc::type::ErrorCode;

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
    resetPerfStats();

    initialized_ = true;
    return R::ok();
}

void Ili9341::flush(const void* buffer, const interface::Rect& /*area*/) {
    if (!initialized_) return;

    // Async update - false = don't wait for redraw
    tft_->update(reinterpret_cast<uint16_t*>(const_cast<void*>(buffer)), false);
}

FLASHMEM void Ili9341::waitAsyncComplete() {
    if (tft_) tft_->waitUpdateAsyncComplete();
}

FLASHMEM Ili9341::PerfSnapshot Ili9341::perfSnapshot() const {
    PerfSnapshot snapshot;
    if (!initialized_ || !tft_) return snapshot;

    snapshot.valid = true;
    snapshot.frames = tft_->statsNbFrames();

    const uint32_t rawCurrentFps = tft_->statsCurrentFPS();
    snapshot.currentFps = (rawCurrentFps == UINT32_MAX) ? 0u : rawCurrentFps;
    snapshot.averageFps = tft_->statsFramerate();

    snapshot.cpuTimeUs = toStatSummary(tft_->statsCPUtimePerFrame());
    snapshot.uploadTimeUs = toStatSummary(tft_->statsUploadtimePerFrame());
    snapshot.pixelsPerFrame = toStatSummary(tft_->statsPixelsPerFrame());
    snapshot.transactionsPerFrame = toStatSummary(tft_->statsTransactionsPerFrame());
    snapshot.marginPerFrame = toStatSummary(tft_->statsMarginPerFrame());
    snapshot.realVSyncSpacing = toStatSummary(tft_->statsRealVSyncSpacing());

    snapshot.pixelsRatio = tft_->statsRatioPixelPerFrame();
    snapshot.diffSpeedUp = tft_->statsDiffSpeedUp();
    snapshot.tearedFrames = tft_->statsNbTeared();
    snapshot.tearRatio = tft_->statsRatioTeared();

    if (snapshot.uploadTimeUs.count > 0 && snapshot.uploadTimeUs.avg > 0.0f) {
        snapshot.uploadRateFps = 1'000'000.0f / snapshot.uploadTimeUs.avg;
    }

    if (diff1_) snapshot.diff1 = toDiffStats(*diff1_);
    if (diff2_) snapshot.diff2 = toDiffStats(*diff2_);

    return snapshot;
}

FLASHMEM void Ili9341::resetPerfStats() {
    if (tft_) tft_->statsReset();
    if (diff1_) diff1_->statsReset();
    if (diff2_) diff2_->statsReset();
}

}  // namespace oc::hal::teensy
