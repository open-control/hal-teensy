#include "Ili9341.hpp"

#include <oc/diagnostics/Performance.hpp>

#if defined(MS_STORAGE_QUALIFICATION)
#include "QualificationTelemetry.hpp"
#endif

namespace oc::hal::teensy {

namespace {

// Keep power-up register programming conservative even when the product uses
// a faster SPI clock for framebuffer transfers. ILI9341_T4 sends its setup
// commands at one quarter of the value passed to begin(), so this caps the
// actual initialization bus at 2.5 MHz. The configured runtime clock is restored
// immediately after the controller has passed its status-register checks.
//
// Some controllers only fail this cold-start handshake intermittently while
// remaining perfectly stable at the configured runtime transfer speed. The
// setup traffic is negligible compared with a frame, so favour boot reliability
// here rather than spending the product's runtime performance margin.
constexpr uint32_t MAX_INITIALIZATION_SPI_SPEED = 10'000'000U;

constexpr uint32_t initializationSpiSpeed(uint32_t runtimeSpiSpeed) {
    return runtimeSpiSpeed < MAX_INITIALIZATION_SPI_SPEED
        ? runtimeSpiSpeed
        : MAX_INITIALIZATION_SPI_SPEED;
}

#if OC_ENABLE_STATS
constexpr uint32_t NATIVE_STATS_SAMPLE_FRAMES = 32U;

uint32_t rectPixelCount(const interface::Rect& rect) {
    const int32_t width = rect.x2 - rect.x1 + 1;
    const int32_t height = rect.y2 - rect.y1 + 1;
    if (width <= 0 || height <= 0) return 0;

    return static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
}

uint64_t statsTotal(const ILI9341_T4::StatsVar& stats) {
    return static_cast<uint64_t>(stats.avg() * stats.count() + 0.5f);
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

    const uint32_t initSpiSpeed = initializationSpiSpeed(config_.spiSpeed);
    if (!tft_->begin(initSpiSpeed)) {
        return R::err({E::HARDWARE_INIT_FAILED, "ILI9341 SPI begin failed"});
    }
    if (initSpiSpeed != config_.spiSpeed) {
        tft_->setSpiClock(static_cast<int>(config_.spiSpeed));
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
    if (config_.refreshRate > 0) {
        tft_->setRefreshRate(static_cast<float>(config_.refreshRate));
    }
    tft_->setDiffGap(config_.diffGap);
    tft_->setIRQPriority(config_.irqPriority);
    tft_->setLateStartRatio(config_.lateStartRatio);
    tft_->clear(0x0000);

    initialized_ = true;
    return R::ok();
}

void Ili9341::flush(const void* buffer, const interface::Rect& area) {
    if (!initialized_ || !buffer) return;

    OC_PERF_SCOPE(perfFlush, "display.ili9341.flush");
    OC_PERF_UNITS(perfFlush, rectPixelCount(area), 1U);
    // Async update - false = don't wait for redraw
#if defined(MS_STORAGE_QUALIFICATION)
    qualification::displayBegin();
#endif
    tft_->update(reinterpret_cast<uint16_t*>(const_cast<void*>(buffer)), false);
#if defined(MS_STORAGE_QUALIFICATION)
    qualification::displayEnd();
#endif
}

void Ili9341::flushRegion(
    const void* frameBuffer,
    const interface::Rect& area,
    uint16_t frameStride,
    bool redrawNow
) {
    if (!initialized_ || !frameBuffer) return;
    if (frameStride < config_.width) return;
    if (area.x1 < 0 || area.y1 < 0 || area.x2 < area.x1 || area.y2 < area.y1) return;
    if (area.x2 >= static_cast<int32_t>(config_.width) ||
        area.y2 >= static_cast<int32_t>(config_.height)) {
        return;
    }

#if OC_ENABLE_STATS
    recordNativePerformance();
#endif

    const auto* frame = static_cast<const uint16_t*>(frameBuffer);
    const auto* region = frame +
        static_cast<size_t>(area.y1) * static_cast<size_t>(frameStride) +
        static_cast<size_t>(area.x1);

    OC_PERF_SCOPE(perfFlush, "display.ili9341.flush-region");
#if defined(MS_STORAGE_QUALIFICATION)
    qualification::displayBegin();
#endif
    tft_->updateRegion(
        redrawNow,
        region,
        static_cast<int>(area.x1),
        static_cast<int>(area.x2),
        static_cast<int>(area.y1),
        static_cast<int>(area.y2),
        static_cast<int>(frameStride)
    );
#if OC_ENABLE_STATS
    uint32_t diffBytes = diff1_ ? static_cast<uint32_t>(diff1_->size()) : 0U;
    if (diff2_ && static_cast<uint32_t>(diff2_->size()) > diffBytes) {
        diffBytes = static_cast<uint32_t>(diff2_->size());
    }
    OC_PERF_UNITS(perfFlush, rectPixelCount(area), diffBytes);
#endif
#if defined(MS_STORAGE_QUALIFICATION)
    qualification::displayEnd();
#endif
}

#if OC_ENABLE_STATS
void Ili9341::recordNativePerformance() {
    const uint32_t frames = tft_->statsNbFrames();
    if (frames < nativeStatsCursor_.frames) nativeStatsCursor_ = {};
    if (frames - nativeStatsCursor_.frames < NATIVE_STATS_SAMPLE_FRAMES) return;

    const uint64_t cpuTimeUs = statsTotal(tft_->statsCPUtimePerFrame());
    const uint64_t uploadTimeUs = statsTotal(tft_->statsUploadtimePerFrame());
    const uint64_t uploadedPixels = statsTotal(tft_->statsPixelsPerFrame());
    const uint64_t transactions = statsTotal(tft_->statsTransactionsPerFrame());
    const uint32_t diffComputations =
        (diff1_ ? diff1_->statsNbComputed() : 0U) +
        (diff2_ ? diff2_->statsNbComputed() : 0U);
    const uint64_t diffTimeUs =
        (diff1_ ? statsTotal(diff1_->statsTime()) : 0U) +
        (diff2_ ? statsTotal(diff2_->statsTime()) : 0U);
    const uint64_t diffBytes =
        (diff1_ ? statsTotal(diff1_->statsSize()) : 0U) +
        (diff2_ ? statsTotal(diff2_->statsSize()) : 0U);
    const uint32_t diffOverflows =
        (diff1_ ? diff1_->statsNbOverflow() : 0U) +
        (diff2_ ? diff2_->statsNbOverflow() : 0U);
    const uint32_t sampledFrames = frames - nativeStatsCursor_.frames;

    if (cpuTimeUs < nativeStatsCursor_.cpuTimeUs ||
        uploadTimeUs < nativeStatsCursor_.uploadTimeUs ||
        uploadedPixels < nativeStatsCursor_.uploadedPixels ||
        transactions < nativeStatsCursor_.transactions ||
        diffComputations < nativeStatsCursor_.diffComputations ||
        diffTimeUs < nativeStatsCursor_.diffTimeUs ||
        diffBytes < nativeStatsCursor_.diffBytes ||
        diffOverflows < nativeStatsCursor_.diffOverflows) {
        nativeStatsCursor_ = {};
        return;
    }

    const uint32_t averagePixels = static_cast<uint32_t>(
        (uploadedPixels - nativeStatsCursor_.uploadedPixels) / sampledFrames
    );
    const uint32_t averageTransactions = static_cast<uint32_t>(
        (transactions - nativeStatsCursor_.transactions) / sampledFrames
    );
    OC_PERF_RECORD(
        "display.ili9341.frame-cpu",
        static_cast<uint32_t>(
            (cpuTimeUs - nativeStatsCursor_.cpuTimeUs) / sampledFrames
        ),
        averagePixels,
        averageTransactions
    );
    OC_PERF_RECORD(
        "display.ili9341.frame-upload",
        static_cast<uint32_t>(
            (uploadTimeUs - nativeStatsCursor_.uploadTimeUs) / sampledFrames
        ),
        averagePixels,
        averageTransactions
    );
    const uint32_t sampledDiffs =
        diffComputations - nativeStatsCursor_.diffComputations;
    if (sampledDiffs > 0U) {
        OC_PERF_RECORD(
            "display.ili9341.diff",
            static_cast<uint32_t>(
                (diffTimeUs - nativeStatsCursor_.diffTimeUs) / sampledDiffs
            ),
            static_cast<uint32_t>(
                (diffBytes - nativeStatsCursor_.diffBytes) / sampledDiffs
            ),
            diffOverflows - nativeStatsCursor_.diffOverflows
        );
    }

    nativeStatsCursor_ = {
        frames,
        cpuTimeUs,
        uploadTimeUs,
        uploadedPixels,
        transactions,
        diffComputations,
        diffTimeUs,
        diffBytes,
        diffOverflows,
    };
}
#endif

FLASHMEM uint32_t Ili9341::panelRefreshRateHz() const {
    if (!tft_) return 0U;
    return static_cast<uint32_t>(tft_->getRefreshRate() + 0.5f);
}

FLASHMEM void Ili9341::waitAsyncComplete() {
    if (tft_) tft_->waitUpdateAsyncComplete();
}

}  // namespace oc::hal::teensy
