#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

#include <oc/type/Result.hpp>
#include <oc/interface/IMidi.hpp>

#include "HighResolutionClock.hpp"

namespace oc::hal::teensy {

struct UsbMidiConfig {
    size_t maxActiveNotes = 32;
};

/**
 * @brief Teensy USB MIDI driver
 */
class UsbMidi : public interface::IMidi {
public:
    static constexpr size_t DEFAULT_MAX_ACTIVE_NOTES = 32;
    static constexpr size_t OUTPUT_QUEUE_CAPACITY = 128;

    UsbMidi() = default;
    explicit UsbMidi(const UsbMidiConfig& config);
    ~UsbMidi() override = default;

    UsbMidi(const UsbMidi&) = delete;
    UsbMidi& operator=(const UsbMidi&) = delete;

    oc::type::Result<void> init() override;
    void update() override;
    void serviceOutput() override;

    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override;
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendSysEx(const uint8_t* data, size_t length) override;
    void sendProgramChange(uint8_t channel, uint8_t program) override;
    void sendPitchBend(uint8_t channel, int16_t value) override;
    void sendChannelPressure(uint8_t channel, uint8_t pressure) override;
    void sendClock() override;
    void sendStart() override;
    void sendStop() override;
    void sendContinue() override;
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
    struct OutputQueueStatsWindow {
        uint32_t windowStartMs = 0;
        uint32_t enqueuedCount = 0;
        uint32_t sentCount = 0;
        uint32_t droppedCount = 0;
        uint32_t maxDepth = 0;
        uint32_t maxDrainUs = 0;

        void reset(uint32_t nowMs) {
            windowStartMs = nowMs;
            enqueuedCount = 0;
            sentCount = 0;
            droppedCount = 0;
            maxDepth = 0;
            maxDrainUs = 0;
        }
    };

    enum class ShortMessageType : uint8_t {
        ControlChange,
        NoteOn,
        NoteOff,
        ProgramChange,
        PitchBend,
        ChannelPressure,
        Clock,
        Start,
        Stop,
        Continue,
    };

    struct QueuedShortMessage {
        ShortMessageType type = ShortMessageType::Clock;
        uint8_t channel = 0;
        uint8_t data1 = 0;
        uint8_t data2 = 0;
        int16_t signedValue = 0;
    };

    struct ActiveNote {
        uint8_t channel;
        uint8_t note;
        bool active;
    };

    bool enqueueShortMessage_(ShortMessageType type, uint8_t channel, uint8_t data1, uint8_t data2);
    bool enqueuePitchBend_(uint8_t channel, int16_t value);
    bool tryDequeueShortMessage_(QueuedShortMessage& message);
    void clearOutputQueue_();
    void drainOutputQueue_();
    void sendShortMessage_(const QueuedShortMessage& message);
    void maybeLogOutputQueueStats_();
    void markNoteActive(uint8_t channel, uint8_t note);
    void markNoteInactive(uint8_t channel, uint8_t note);
    uint64_t nowUs_();

    CCCallback on_cc_;
    NoteCallback on_note_on_;
    NoteCallback on_note_off_;
    SysExCallback on_sysex_;
    ClockCallback on_clock_;
    RealtimeCallback on_start_;
    RealtimeCallback on_stop_;
    RealtimeCallback on_continue_;

    std::vector<ActiveNote> active_notes_;
    std::array<QueuedShortMessage, OUTPUT_QUEUE_CAPACITY> output_queue_{};
    size_t output_queue_head_ = 0;
    size_t output_queue_tail_ = 0;
    size_t output_queue_count_ = 0;
    OutputQueueStatsWindow output_stats_{};
    size_t max_active_notes_ = DEFAULT_MAX_ACTIVE_NOTES;
    bool initialized_ = false;
    HighResolutionClock clock_{};
};

}  // namespace oc::hal::teensy
