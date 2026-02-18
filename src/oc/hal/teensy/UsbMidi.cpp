#include "UsbMidi.hpp"

#include <Arduino.h>

namespace oc::hal::teensy {

UsbMidi::UsbMidi(const UsbMidiConfig& config)
    : max_active_notes_(config.maxActiveNotes) {}

oc::type::Result<void> UsbMidi::init() {
    if (initialized_) return oc::type::Result<void>::ok();

    active_notes_.resize(max_active_notes_);
    for (auto& note : active_notes_) {
        note.active = false;
    }

    initialized_ = true;
    return oc::type::Result<void>::ok();
}

void UsbMidi::update() {
    if (!initialized_) return;

    while (usbMIDI.read()) {
        const uint64_t timestampUs = nowUs_();
        uint8_t type = usbMIDI.getType();
        uint8_t channel = usbMIDI.getChannel() - 1;
        uint8_t data1 = usbMIDI.getData1();
        uint8_t data2 = usbMIDI.getData2();

        switch (type) {
            case usbMIDI.ControlChange:
                if (on_cc_) on_cc_(channel, data1, data2);
                break;
            case usbMIDI.NoteOn:
                if (on_note_on_) on_note_on_(channel, data1, data2);
                break;
            case usbMIDI.NoteOff:
                if (on_note_off_) on_note_off_(channel, data1, data2);
                break;
            case usbMIDI.SystemExclusive:
                if (on_sysex_) {
                    on_sysex_(usbMIDI.getSysExArray(), usbMIDI.getSysExArrayLength());
                }
                break;
            case usbMIDI.Clock:
                if (on_clock_) on_clock_(timestampUs);
                break;
            case usbMIDI.Start:
                if (on_start_) on_start_();
                break;
            case usbMIDI.Continue:
                if (on_continue_) on_continue_();
                break;
            case usbMIDI.Stop:
                if (on_stop_) on_stop_();
                break;
            default:
                break;
        }
    }
}

void UsbMidi::markNoteActive(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (!slot.active) {
            slot = {channel, note, true};
            return;
        }
    }
    if (!active_notes_.empty()) {
        active_notes_[0] = {channel, note, true};
    }
}

void UsbMidi::markNoteInactive(uint8_t channel, uint8_t note) {
    for (auto& slot : active_notes_) {
        if (slot.active && slot.channel == channel && slot.note == note) {
            slot.active = false;
            return;
        }
    }
}

void UsbMidi::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    usbMIDI.sendControlChange(cc, value, channel + 1);
}

void UsbMidi::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    markNoteActive(channel, note);
    usbMIDI.sendNoteOn(note, velocity, channel + 1);
}

void UsbMidi::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    markNoteInactive(channel, note);
    usbMIDI.sendNoteOff(note, velocity, channel + 1);
}

void UsbMidi::sendSysEx(const uint8_t* data, size_t length) {
    usbMIDI.sendSysEx(length, data, true);
}

void UsbMidi::sendProgramChange(uint8_t channel, uint8_t program) {
    usbMIDI.sendProgramChange(program, channel + 1);
}

void UsbMidi::sendPitchBend(uint8_t channel, int16_t value) {
    usbMIDI.sendPitchBend(value, channel + 1);
}

void UsbMidi::sendChannelPressure(uint8_t channel, uint8_t pressure) {
    usbMIDI.sendAfterTouch(pressure, channel + 1);
}

void UsbMidi::sendClock() {
    usbMIDI.sendRealTime(usbMIDI.Clock);
}

void UsbMidi::sendStart() {
    usbMIDI.sendRealTime(usbMIDI.Start);
}

void UsbMidi::sendStop() {
    usbMIDI.sendRealTime(usbMIDI.Stop);
}

void UsbMidi::sendContinue() {
    usbMIDI.sendRealTime(usbMIDI.Continue);
}

void UsbMidi::allNotesOff() {
    for (auto& slot : active_notes_) {
        if (slot.active) {
            usbMIDI.sendNoteOff(slot.note, 0, slot.channel + 1);
            slot.active = false;
        }
    }
}

uint64_t UsbMidi::nowUs_() {
    const uint32_t raw = micros();

    if (!micros_initialized_) {
        micros_initialized_ = true;
        last_micros_32_ = raw;
        return static_cast<uint64_t>(raw);
    }

    if (raw < last_micros_32_) {
        micros_wraps_ += 1;
    }
    last_micros_32_ = raw;

    return (micros_wraps_ << 32) | static_cast<uint64_t>(raw);
}

void UsbMidi::setOnCC(CCCallback cb) { on_cc_ = cb; }
void UsbMidi::setOnNoteOn(NoteCallback cb) { on_note_on_ = cb; }
void UsbMidi::setOnNoteOff(NoteCallback cb) { on_note_off_ = cb; }
void UsbMidi::setOnSysEx(SysExCallback cb) { on_sysex_ = cb; }
void UsbMidi::setOnClock(ClockCallback cb) { on_clock_ = cb; }
void UsbMidi::setOnStart(RealtimeCallback cb) { on_start_ = cb; }
void UsbMidi::setOnStop(RealtimeCallback cb) { on_stop_ = cb; }
void UsbMidi::setOnContinue(RealtimeCallback cb) { on_continue_ = cb; }

}  // namespace oc::hal::teensy
