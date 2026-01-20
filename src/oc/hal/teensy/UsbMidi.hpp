#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <oc/types/Result.hpp>
#include <oc/interface/IMidi.hpp>

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

    UsbMidi() = default;
    explicit UsbMidi(const UsbMidiConfig& config);
    ~UsbMidi() override = default;

    UsbMidi(const UsbMidi&) = delete;
    UsbMidi& operator=(const UsbMidi&) = delete;

    oc::Result<void> init() override;
    void update() override;

    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override;
    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override;
    void sendSysEx(const uint8_t* data, size_t length) override;
    void sendProgramChange(uint8_t channel, uint8_t program) override;
    void sendPitchBend(uint8_t channel, int16_t value) override;
    void sendChannelPressure(uint8_t channel, uint8_t pressure) override;
    void allNotesOff() override;

    void setOnCC(CCCallback cb) override;
    void setOnNoteOn(NoteCallback cb) override;
    void setOnNoteOff(NoteCallback cb) override;
    void setOnSysEx(SysExCallback cb) override;

private:
    struct ActiveNote {
        uint8_t channel;
        uint8_t note;
        bool active;
    };

    void markNoteActive(uint8_t channel, uint8_t note);
    void markNoteInactive(uint8_t channel, uint8_t note);

    CCCallback on_cc_;
    NoteCallback on_note_on_;
    NoteCallback on_note_off_;
    SysExCallback on_sysex_;

    std::vector<ActiveNote> active_notes_;
    size_t max_active_notes_ = DEFAULT_MAX_ACTIVE_NOTES;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
