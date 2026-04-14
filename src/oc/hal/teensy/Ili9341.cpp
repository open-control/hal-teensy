#include "Ili9341.hpp"

#include <oc/log/Log.hpp>

namespace oc::hal::teensy {

namespace {

#if defined(PERF_LOG)
struct FlushPerfWindow {
    uint32_t startedAtMs = 0;
    uint32_t flushCount = 0;
    uint32_t rectPixels = 0;
    uint32_t fullFrameRects = 0;
    uint32_t flushCallTimeUs = 0;
    uint32_t maxFlushCallUs = 0;
};
#endif

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

#if defined(PERF_LOG)
uint32_t rectPixelCount(const interface::Rect& rect) {
    const int32_t width = rect.x2 - rect.x1 + 1;
    const int32_t height = rect.y2 - rect.y1 + 1;
    if (width <= 0 || height <= 0) return 0;

    return static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
}
#endif

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

#if defined(PERF_LOG)
    OC_LOG_INFO(
        "[Perf][Display][Config] framebufferBytes={} diff1Bytes={} diff2Bytes={} "
        "spiHz={} refreshHz={} vsyncSpacing={} diffGap={}",
        static_cast<uint32_t>(config_.framebufferSize() * sizeof(uint16_t)),
        static_cast<uint32_t>(effectiveDiff1Size_),
        static_cast<uint32_t>(diff2_ ? effectiveDiff2Size_ : 0),
        config_.spiSpeed,
        config_.refreshRate,
        config_.vsyncSpacing,
        config_.diffGap
    );
#endif

    initialized_ = true;
    return R::ok();
}

void Ili9341::flush(const void* buffer, const interface::Rect& area) {
    if (!initialized_) return;

#if defined(PERF_LOG)
    static FlushPerfWindow perfWindow;
    const uint32_t nowMs = oc::log::getTimeMs();
    if (perfWindow.startedAtMs == 0) {
        perfWindow.startedAtMs = nowMs;
    }

    const uint32_t flushStartUs = micros();
#endif
    // Async update - false = don't wait for redraw
    tft_->update(reinterpret_cast<uint16_t*>(const_cast<void*>(buffer)), false);
#if defined(PERF_LOG)
    const uint32_t flushCallUs = micros() - flushStartUs;
    const uint32_t rectPixels = rectPixelCount(area);
    ++perfWindow.flushCount;
    perfWindow.rectPixels += rectPixels;
    perfWindow.flushCallTimeUs += flushCallUs;
    if (flushCallUs > perfWindow.maxFlushCallUs) {
        perfWindow.maxFlushCallUs = flushCallUs;
    }

    if (area.x1 == 0 && area.y1 == 0 &&
        area.x2 == static_cast<int32_t>(config_.width) - 1 &&
        area.y2 == static_cast<int32_t>(config_.height) - 1) {
        ++perfWindow.fullFrameRects;
    }

    if ((nowMs - perfWindow.startedAtMs) >= 1000) {
        const auto snapshot = perfSnapshot();
        const uint32_t avgFlushCallUs =
            (perfWindow.flushCount > 0) ? (perfWindow.flushCallTimeUs / perfWindow.flushCount) : 0;

        OC_LOG_INFO(
            "[Perf][Display][ILI9341] flushes={} rectPx={} fullFrameRects={} "
            "avgFlushCall={}us maxFlushCall={}us frames={} fps={} uploadAvg={}us "
            "uploadMax={}us pixelsFrameAvg={} txAvg={} marginAvg={} diffSpeedUp={} "
            "pixelsRatio={} tearRatio={}",
            perfWindow.flushCount,
            perfWindow.rectPixels,
            perfWindow.fullFrameRects,
            avgFlushCallUs,
            perfWindow.maxFlushCallUs,
            snapshot.frames,
            snapshot.currentFps,
            snapshot.uploadTimeUs.avg,
            snapshot.uploadTimeUs.max,
            snapshot.pixelsPerFrame.avg,
            snapshot.transactionsPerFrame.avg,
            snapshot.marginPerFrame.avg,
            snapshot.diffSpeedUp,
            snapshot.pixelsRatio,
            snapshot.tearRatio
        );

        OC_LOG_INFO(
            "[Perf][Display][ILI9341][Diff] diff1Computed={} diff1Overflow={} diff1SizeAvg={}B "
            "diff1TimeAvg={}us diff2Computed={} diff2Overflow={} diff2SizeAvg={}B diff2TimeAvg={}us",
            snapshot.diff1.computed,
            snapshot.diff1.overflow,
            snapshot.diff1.sizeBytes.avg,
            snapshot.diff1.computeTimeUs.avg,
            snapshot.diff2.computed,
            snapshot.diff2.overflow,
            snapshot.diff2.sizeBytes.avg,
            snapshot.diff2.computeTimeUs.avg
        );

        resetPerfStats();
        perfWindow = {};
        perfWindow.startedAtMs = nowMs;
    }
#endif
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
