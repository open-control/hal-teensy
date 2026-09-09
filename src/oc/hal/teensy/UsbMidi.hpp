#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include <oc/type/Result.hpp>
#include <oc/Config.hpp>
#include <oc/interface/IMidi.hpp>

#include "HighResolutionClock.hpp"

namespace oc::hal::teensy {

/**
 * @brief Teensy USB MIDI driver
 */
class UsbMidi : public interface::IMidi {
public:
    // One high-speed USB MIDI receive packet contains at most 512 / 4 = 128
    // MIDI event packets. Process at most one such packet per app turn so a
    // continuously producing host cannot monopolize the foreground loop.
    static constexpr size_t MAX_INPUT_MESSAGES_PER_POLL = 128U;
    static constexpr size_t OUTPUT_QUEUE_CAPACITY = 128;
    // Complete F0...F7 messages are copied atomically into the same packet FIFO.
    static constexpr size_t MAX_SYSEX_BYTES = OUTPUT_QUEUE_CAPACITY * 3;
    static constexpr uint32_t DEFAULT_OUTPUT_DRAIN_BUDGET_US = 500;
    // Below the musical timer/USB (128), above display DMA (160).
    static constexpr uint8_t OUTPUT_IRQ_PRIORITY = 144;
    static constexpr uint32_t OUTPUT_IRQ_BUDGET_US = 250;
    static constexpr uint32_t OUTPUT_REFILL_BUDGET_US = 40;

    UsbMidi() = default;
    ~UsbMidi() override;

    UsbMidi(const UsbMidi&) = delete;
    UsbMidi& operator=(const UsbMidi&) = delete;

    oc::type::Result<void> init() override;
    void update() override;
    void pollInput() override;
    void serviceOutput() override;
    void serviceOutput(uint32_t budgetUs) override;
    void setOutputRefill(OutputRefill callback, void* context) override;
#if OC_ENABLE_STATS
    // Foreground-only, cumulative since boot; includes unsupported input types.
    static uint32_t receivedMessageCount() { return received_message_count_; }
#endif

    interface::MidiOutputAcceptance sendCC(uint8_t channel, uint8_t cc, uint8_t value) override;
    interface::MidiOutputAcceptance sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override;
    interface::MidiOutputAcceptance sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override;
    interface::MidiOutputAcceptance sendSysEx(const uint8_t* data, size_t length) override;
    interface::MidiOutputAcceptance sendProgramChange(uint8_t channel, uint8_t program) override;
    interface::MidiOutputAcceptance sendPitchBend(uint8_t channel, int16_t value) override;
    interface::MidiOutputAcceptance sendChannelPressure(uint8_t channel, uint8_t pressure) override;
    interface::MidiOutputAcceptance sendClock() override;
    interface::MidiOutputAcceptance sendStart() override;
    interface::MidiOutputAcceptance sendStop() override;
    interface::MidiOutputAcceptance sendContinue() override;
    void allNotesOff() override;

    void setOnCC(CCCallback cb) override;
    void setOnNoteOn(NoteCallback cb) override;
    void setOnNoteOff(NoteCallback cb) override;
    void setOnSysEx(SysExCallback cb) override;
    void setOnClock(ClockCallback cb) override;
    void setOnStart(RealtimeCallback cb) override;
    void setOnStop(RealtimeCallback cb) override;
    void setOnContinue(RealtimeCallback cb) override;

private:
    struct QueuedPacket {
        uint32_t data = 0;
#if OC_ENABLE_STATS
        uint32_t admittedUs = 0;
#endif
    };
    static_assert(sizeof(QueuedPacket) == sizeof(uint32_t) * (OC_ENABLE_STATS ? 2 : 1));

    static constexpr uint8_t MIDI_CHANNEL_COUNT = 16;
    static constexpr uint8_t MIDI_NOTE_COUNT = 128;
    static constexpr uint8_t ACTIVE_NOTE_WORD_BITS = 32;
    static constexpr uint8_t ACTIVE_NOTE_WORD_COUNT =
        MIDI_NOTE_COUNT / ACTIVE_NOTE_WORD_BITS;
    using ActiveNoteMask = std::array<uint32_t, ACTIVE_NOTE_WORD_COUNT>;

    interface::MidiOutputAcceptance enqueueShortMessage_(uint8_t status, uint8_t channel,
                                                         uint8_t data1, uint8_t data2);
    bool canEnqueue_(size_t count);
    void appendPacket_(uint32_t data);
    bool peekPacket_(QueuedPacket& packet);
    void clearOutputQueue_();
    void drainOutputQueue_(uint32_t budgetUs);
    void processOutputInterrupt_();
    static void requestOutput_();
    void reconcileOutputSession_();
    bool nextPanicPacket_(QueuedPacket& packet);
    void acknowledgePacket_(uint32_t packet);
    void reportInputBudgetHits_();
    void reportOutputRejections_();
    uint64_t nowUs_();

    CCCallback on_cc_;
    NoteCallback on_note_on_;
    NoteCallback on_note_off_;
    SysExCallback on_sysex_;
    ClockCallback on_clock_;
    RealtimeCallback on_start_;
    RealtimeCallback on_stop_;
    RealtimeCallback on_continue_;

    std::array<ActiveNoteMask, MIDI_CHANNEL_COUNT> active_notes_{};
    std::array<QueuedPacket, OUTPUT_QUEUE_CAPACITY> output_queue_{};
    size_t output_queue_head_ = 0;
    size_t output_queue_tail_ = 0;
    size_t output_queue_count_ = 0;
#if OC_ENABLE_STATS
    inline static uint32_t received_message_count_ = 0;
    size_t output_queue_high_water_ = 0;
    uint32_t last_output_service_us_ = 0;
    bool output_service_seen_ = false;
    uint32_t output_wake_us_ = 0;
    bool output_wake_pending_ = false;
#endif
    uint32_t input_budget_hit_count_ = 0;
    uint32_t last_input_budget_report_ms_ = 0;
    volatile uint32_t rejected_output_count_ = 0;
    uint32_t last_rejection_report_ms_ = 0;
    uint32_t output_session_ = 0;
    uint32_t cancelled_output_count_ = 0;
    uint32_t output_error_count_ = 0;
    OutputRefill output_refill_ = nullptr;
    void* output_refill_context_ = nullptr;
    inline static UsbMidi* output_owner_ = nullptr;
    uint8_t panic_word_ = 0;
    bool panic_pending_ = false;
    bool panic_requested_ = false;
    bool output_blocked_ = false;
    bool output_servicing_ = false;
    bool sysex_open_ = false;
    bool initialized_ = false;
    HighResolutionClock clock_{};
};

}  // namespace oc::hal::teensy
