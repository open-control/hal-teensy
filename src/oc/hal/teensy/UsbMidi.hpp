#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

#include <oc/type/Result.hpp>
#include <oc/interface/IMidi.hpp>

#include "HighResolutionClock.hpp"

namespace oc::hal::teensy {

/**
 * @brief Teensy USB MIDI driver
 */
class UsbMidi : public interface::IMidi {
public:
    static constexpr size_t OUTPUT_QUEUE_CAPACITY = 128;
    static constexpr uint32_t DEFAULT_OUTPUT_DRAIN_BUDGET_US = 500;

    UsbMidi() = default;
    ~UsbMidi() override = default;

    UsbMidi(const UsbMidi&) = delete;
    UsbMidi& operator=(const UsbMidi&) = delete;

    oc::type::Result<void> init() override;
    void update() override;
    void pollInput() override;
    void serviceOutput() override;
    void serviceOutput(uint32_t budgetUs) override;

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

    static constexpr uint8_t MIDI_CHANNEL_COUNT = 16;
    static constexpr uint8_t MIDI_NOTE_COUNT = 128;
    static constexpr uint8_t ACTIVE_NOTE_WORD_BITS = 32;
    static constexpr uint8_t ACTIVE_NOTE_WORD_COUNT =
        MIDI_NOTE_COUNT / ACTIVE_NOTE_WORD_BITS;
    using ActiveNoteMask = std::array<uint32_t, ACTIVE_NOTE_WORD_COUNT>;

    bool enqueueShortMessage_(ShortMessageType type, uint8_t channel, uint8_t data1, uint8_t data2);
    bool enqueuePitchBend_(uint8_t channel, int16_t value);
    bool tryDequeueShortMessage_(QueuedShortMessage& message);
    void clearOutputQueue_();
    void drainOutputQueue_(uint32_t budgetUs);
    void sendShortMessage_(const QueuedShortMessage& message);
    void reportOutputRejections_();
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

    std::array<ActiveNoteMask, MIDI_CHANNEL_COUNT> active_notes_{};
    std::array<QueuedShortMessage, OUTPUT_QUEUE_CAPACITY> output_queue_{};
    size_t output_queue_head_ = 0;
    size_t output_queue_tail_ = 0;
    size_t output_queue_count_ = 0;
    volatile uint32_t rejected_output_count_ = 0;
    uint32_t last_rejection_report_ms_ = 0;
    bool initialized_ = false;
    HighResolutionClock clock_{};
};

}  // namespace oc::hal::teensy
