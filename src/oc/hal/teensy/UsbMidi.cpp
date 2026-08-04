#include "UsbMidi.hpp"

#include <Arduino.h>

#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>

#if defined(MS_STORAGE_QUALIFICATION)
#include "QualificationTelemetry.hpp"
#endif

namespace oc::hal::teensy {

namespace {

using oc::interface::MidiOutputAcceptance;

inline uint32_t readPrimask() {
    uint32_t primask = 0;
    asm volatile("MRS %0, primask" : "=r"(primask));
    return primask;
}

inline uint32_t readIpsr() {
    uint32_t ipsr = 0;
    asm volatile("MRS %0, ipsr" : "=r"(ipsr));
    return ipsr;
}

class InterruptLock {
public:
    InterruptLock()
        : primask_(readPrimask()) {
        __disable_irq();
    }

    ~InterruptLock() {
        asm volatile("MSR primask, %0" : : "r"(primask_) : "memory");
    }

    InterruptLock(const InterruptLock&) = delete;
    InterruptLock& operator=(const InterruptLock&) = delete;

private:
    uint32_t primask_ = 0;
};

}  // namespace

FLASHMEM oc::type::Result<void> UsbMidi::init() {
    if (initialized_) return oc::type::Result<void>::ok();

    initialized_ = true;
    return oc::type::Result<void>::ok();
}

void UsbMidi::update() {
    if (!initialized_) return;

    pollInput();
    serviceOutput();
}

void UsbMidi::pollInput() {
    if (!initialized_) return;

    while (usbMIDI.read()) {
        const uint64_t timestampUs = nowUs_();
        uint8_t type = usbMIDI.getType();
        uint8_t channel = usbMIDI.getChannel() - 1;
        uint8_t data1 = usbMIDI.getData1();
        uint8_t data2 = usbMIDI.getData2();

#if defined(MS_STORAGE_QUALIFICATION)
        const auto trafficKind = type == usbMIDI.Clock
            ? qualification::MidiTrafficKind::Clock
            : (type == usbMIDI.NoteOff
                   ? qualification::MidiTrafficKind::NoteOff
                   : qualification::MidiTrafficKind::Other);
        qualification::noteMidiInput(trafficKind);
#endif

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

    reportOutputRejections_();
}

void UsbMidi::serviceOutput() {
    serviceOutput(DEFAULT_OUTPUT_DRAIN_BUDGET_US);
}

void UsbMidi::serviceOutput(uint32_t budgetUs) {
    // Teensy USB MIDI may wait for the host for up to 40 ms. Physical output
    // therefore belongs exclusively to the foreground loop, never an ISR.
    if (!initialized_ || readIpsr() != 0U) return;
    drainOutputQueue_(budgetUs);
    reportOutputRejections_();
}

void UsbMidi::markNoteActive(uint8_t channel, uint8_t note) {
    channel &= 0x0FU;
    note &= 0x7FU;
    active_notes_[channel][note / ACTIVE_NOTE_WORD_BITS] |=
        (1UL << (note % ACTIVE_NOTE_WORD_BITS));
}

void UsbMidi::markNoteInactive(uint8_t channel, uint8_t note) {
    channel &= 0x0FU;
    note &= 0x7FU;
    active_notes_[channel][note / ACTIVE_NOTE_WORD_BITS] &=
        ~(1UL << (note % ACTIVE_NOTE_WORD_BITS));
}

MidiOutputAcceptance UsbMidi::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    return enqueueShortMessage_(ShortMessageType::ControlChange, channel, cc, value)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return enqueueShortMessage_(ShortMessageType::NoteOn, channel, note, velocity)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return enqueueShortMessage_(ShortMessageType::NoteOff, channel, note, velocity)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendSysEx(const uint8_t* data, size_t length) {
    if (!initialized_ || readIpsr() != 0U) {
        return MidiOutputAcceptance::REJECTED;
    }
#if defined(MS_STORAGE_QUALIFICATION)
    qualification::midiOutputPulse(qualification::MidiTrafficKind::Other);
#endif
    usbMIDI.sendSysEx(length, data, true);
    return MidiOutputAcceptance::ACCEPTED;
}

MidiOutputAcceptance UsbMidi::sendProgramChange(uint8_t channel, uint8_t program) {
    return enqueueShortMessage_(ShortMessageType::ProgramChange, channel, program, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendPitchBend(uint8_t channel, int16_t value) {
    return enqueuePitchBend_(channel, value)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendChannelPressure(uint8_t channel, uint8_t pressure) {
    return enqueueShortMessage_(ShortMessageType::ChannelPressure, channel, pressure, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendClock() {
    return enqueueShortMessage_(ShortMessageType::Clock, 0, 0, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendStart() {
    return enqueueShortMessage_(ShortMessageType::Start, 0, 0, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendStop() {
    return enqueueShortMessage_(ShortMessageType::Stop, 0, 0, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

MidiOutputAcceptance UsbMidi::sendContinue() {
    return enqueueShortMessage_(ShortMessageType::Continue, 0, 0, 0)
        ? MidiOutputAcceptance::ACCEPTED
        : MidiOutputAcceptance::REJECTED;
}

void UsbMidi::allNotesOff() {
    if (!initialized_ || readIpsr() != 0U) return;
    clearOutputQueue_();

    for (uint8_t channel = 0; channel < active_notes_.size(); ++channel) {
        auto& activeNotes = active_notes_[channel];
        for (uint8_t note = 0; note < MIDI_NOTE_COUNT; ++note) {
            const uint32_t noteBit = 1UL << (note % ACTIVE_NOTE_WORD_BITS);
            auto& word = activeNotes[note / ACTIVE_NOTE_WORD_BITS];
            if ((word & noteBit) == 0) continue;
#if defined(MS_STORAGE_QUALIFICATION)
            qualification::midiOutputPulse(qualification::MidiTrafficKind::NoteOff);
#endif
            usbMIDI.sendNoteOff(note, 0, channel + 1);
            word &= ~noteBit;
        }
    }

    usbMIDI.send_now();
}

uint64_t UsbMidi::nowUs_() {
    return clock_.micros64();
}

bool UsbMidi::enqueueShortMessage_(ShortMessageType type,
                                   uint8_t channel,
                                   uint8_t data1,
                                   uint8_t data2) {
    if (!initialized_) {
        return false;
    }

    InterruptLock lock;
    if (output_queue_count_ >= output_queue_.size()) {
        rejected_output_count_ = rejected_output_count_ + 1U;
#if defined(MS_STORAGE_QUALIFICATION)
        qualification::noteMidiOutputDrop();
#endif
        return false;
    }

    output_queue_[output_queue_tail_] = {
        .type = type,
        .channel = channel,
        .data1 = data1,
        .data2 = data2,
    };
    output_queue_tail_ = (output_queue_tail_ + 1U) % output_queue_.size();
    output_queue_count_ += 1U;
    return true;
}

bool UsbMidi::enqueuePitchBend_(uint8_t channel, int16_t value) {
    if (!initialized_) {
        return false;
    }

    InterruptLock lock;
    if (output_queue_count_ >= output_queue_.size()) {
        rejected_output_count_ = rejected_output_count_ + 1U;
#if defined(MS_STORAGE_QUALIFICATION)
        qualification::noteMidiOutputDrop();
#endif
        return false;
    }

    output_queue_[output_queue_tail_] = {
        .type = ShortMessageType::PitchBend,
        .channel = channel,
        .signedValue = value,
    };
    output_queue_tail_ = (output_queue_tail_ + 1U) % output_queue_.size();
    output_queue_count_ += 1U;
    return true;
}

bool UsbMidi::tryDequeueShortMessage_(QueuedShortMessage& message) {
    InterruptLock lock;
    if (output_queue_count_ == 0) {
        return false;
    }

    message = output_queue_[output_queue_head_];
    output_queue_head_ = (output_queue_head_ + 1U) % output_queue_.size();
    output_queue_count_ -= 1U;
    return true;
}

void UsbMidi::clearOutputQueue_() {
    InterruptLock lock;
    output_queue_head_ = 0;
    output_queue_tail_ = 0;
    output_queue_count_ = 0;
}

void UsbMidi::drainOutputQueue_(uint32_t budgetUs) {
    QueuedShortMessage message;
    if (!tryDequeueShortMessage_(message)) {
        return;
    }

    const uint32_t drainStartUs = static_cast<uint32_t>(nowUs_());
#if OC_ENABLE_STATS
    uint32_t sentCount = 0;
#endif

    do {
        sendShortMessage_(message);
#if OC_ENABLE_STATS
        sentCount += 1U;
#endif

        if ((static_cast<uint32_t>(nowUs_()) - drainStartUs) >= budgetUs) {
            break;
        }
    } while (tryDequeueShortMessage_(message));

    usbMIDI.send_now();
#if OC_ENABLE_STATS
    const uint32_t elapsedUs = static_cast<uint32_t>(nowUs_()) - drainStartUs;
    if (readIpsr() == 0U) {
        OC_PERF_RECORD("midi.usb-output-drain", elapsedUs, sentCount, budgetUs);
    }
#endif
}

void UsbMidi::sendShortMessage_(const QueuedShortMessage& message) {
#if defined(MS_STORAGE_QUALIFICATION)
    const auto trafficKind = message.type == ShortMessageType::Clock
        ? qualification::MidiTrafficKind::Clock
        : (message.type == ShortMessageType::NoteOff
               ? qualification::MidiTrafficKind::NoteOff
               : qualification::MidiTrafficKind::Other);
    qualification::midiOutputPulse(trafficKind);
#endif

    switch (message.type) {
        case ShortMessageType::ControlChange:
            usbMIDI.sendControlChange(message.data1, message.data2, message.channel + 1);
            break;
        case ShortMessageType::NoteOn:
            markNoteActive(message.channel, message.data1);
            usbMIDI.sendNoteOn(message.data1, message.data2, message.channel + 1);
            break;
        case ShortMessageType::NoteOff:
            markNoteInactive(message.channel, message.data1);
            usbMIDI.sendNoteOff(message.data1, message.data2, message.channel + 1);
            break;
        case ShortMessageType::ProgramChange:
            usbMIDI.sendProgramChange(message.data1, message.channel + 1);
            break;
        case ShortMessageType::PitchBend:
            usbMIDI.sendPitchBend(message.signedValue, message.channel + 1);
            break;
        case ShortMessageType::ChannelPressure:
            usbMIDI.sendAfterTouch(message.data1, message.channel + 1);
            break;
        case ShortMessageType::Clock:
            usbMIDI.sendRealTime(usbMIDI.Clock);
            break;
        case ShortMessageType::Start:
            usbMIDI.sendRealTime(usbMIDI.Start);
            break;
        case ShortMessageType::Stop:
            usbMIDI.sendRealTime(usbMIDI.Stop);
            break;
        case ShortMessageType::Continue:
            usbMIDI.sendRealTime(usbMIDI.Continue);
            break;
    }
}

void UsbMidi::reportOutputRejections_() {
    if (rejected_output_count_ == 0) return;

    const uint32_t nowMs = millis();
    if (last_rejection_report_ms_ != 0 &&
        (nowMs - last_rejection_report_ms_) < 1000U) {
        return;
    }

    uint32_t rejected = 0;
    {
        InterruptLock lock;
        rejected = rejected_output_count_;
        rejected_output_count_ = 0;
    }
    if (rejected > 0) {
        last_rejection_report_ms_ = nowMs;
        OC_LOG_WARN("UsbMidi output queue rejected {} message(s)", rejected);
    }
}

FLASHMEM void UsbMidi::setOnCC(CCCallback cb) { on_cc_ = cb; }
FLASHMEM void UsbMidi::setOnNoteOn(NoteCallback cb) { on_note_on_ = cb; }
FLASHMEM void UsbMidi::setOnNoteOff(NoteCallback cb) { on_note_off_ = cb; }
FLASHMEM void UsbMidi::setOnSysEx(SysExCallback cb) { on_sysex_ = cb; }
FLASHMEM void UsbMidi::setOnClock(ClockCallback cb) { on_clock_ = cb; }
FLASHMEM void UsbMidi::setOnStart(RealtimeCallback cb) { on_start_ = cb; }
FLASHMEM void UsbMidi::setOnStop(RealtimeCallback cb) { on_stop_ = cb; }
FLASHMEM void UsbMidi::setOnContinue(RealtimeCallback cb) { on_continue_ = cb; }

}  // namespace oc::hal::teensy
