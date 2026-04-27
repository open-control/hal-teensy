#include "UsbMidi.hpp"

#include <algorithm>

#include <Arduino.h>

#include <oc/log/Log.hpp>

namespace oc::hal::teensy {

namespace {

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

FLASHMEM UsbMidi::UsbMidi(const UsbMidiConfig& config)
    : max_active_notes_(config.maxActiveNotes) {}

FLASHMEM oc::type::Result<void> UsbMidi::init() {
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

    maybeLogOutputQueueStats_();
}

void UsbMidi::serviceOutput() {
    serviceOutput(DEFAULT_OUTPUT_DRAIN_BUDGET_US);
}

void UsbMidi::serviceOutput(uint32_t budgetUs) {
    if (!initialized_) return;
    drainOutputQueue_(budgetUs);
    if (readIpsr() == 0U) {
        maybeLogOutputQueueStats_();
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
    enqueueShortMessage_(ShortMessageType::ControlChange, channel, cc, value);
}

void UsbMidi::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    enqueueShortMessage_(ShortMessageType::NoteOn, channel, note, velocity);
}

void UsbMidi::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    enqueueShortMessage_(ShortMessageType::NoteOff, channel, note, velocity);
}

void UsbMidi::sendSysEx(const uint8_t* data, size_t length) {
    usbMIDI.sendSysEx(length, data, true);
}

void UsbMidi::sendProgramChange(uint8_t channel, uint8_t program) {
    enqueueShortMessage_(ShortMessageType::ProgramChange, channel, program, 0);
}

void UsbMidi::sendPitchBend(uint8_t channel, int16_t value) {
    enqueuePitchBend_(channel, value);
}

void UsbMidi::sendChannelPressure(uint8_t channel, uint8_t pressure) {
    enqueueShortMessage_(ShortMessageType::ChannelPressure, channel, pressure, 0);
}

void UsbMidi::sendClock() {
    enqueueShortMessage_(ShortMessageType::Clock, 0, 0, 0);
}

void UsbMidi::sendStart() {
    enqueueShortMessage_(ShortMessageType::Start, 0, 0, 0);
}

void UsbMidi::sendStop() {
    enqueueShortMessage_(ShortMessageType::Stop, 0, 0, 0);
}

void UsbMidi::sendContinue() {
    enqueueShortMessage_(ShortMessageType::Continue, 0, 0, 0);
}

void UsbMidi::allNotesOff() {
    clearOutputQueue_();

    for (auto& slot : active_notes_) {
        if (slot.active) {
            usbMIDI.sendNoteOff(slot.note, 0, slot.channel + 1);
            slot.active = false;
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
        output_stats_.droppedCount += 1U;
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
    output_stats_.enqueuedCount += 1U;
    output_stats_.maxDepth =
        std::max(output_stats_.maxDepth, static_cast<uint32_t>(output_queue_count_));
    return true;
}

bool UsbMidi::enqueuePitchBend_(uint8_t channel, int16_t value) {
    if (!initialized_) {
        return false;
    }

    InterruptLock lock;
    if (output_queue_count_ >= output_queue_.size()) {
        output_stats_.droppedCount += 1U;
        return false;
    }

    output_queue_[output_queue_tail_] = {
        .type = ShortMessageType::PitchBend,
        .channel = channel,
        .signedValue = value,
    };
    output_queue_tail_ = (output_queue_tail_ + 1U) % output_queue_.size();
    output_queue_count_ += 1U;
    output_stats_.enqueuedCount += 1U;
    output_stats_.maxDepth =
        std::max(output_stats_.maxDepth, static_cast<uint32_t>(output_queue_count_));
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
    uint32_t sentCount = 0;

    do {
        sendShortMessage_(message);
        sentCount += 1U;

        if ((static_cast<uint32_t>(nowUs_()) - drainStartUs) >= budgetUs) {
            break;
        }
    } while (tryDequeueShortMessage_(message));

    usbMIDI.send_now();
    output_stats_.sentCount += sentCount;
    output_stats_.maxDrainUs = std::max(
        output_stats_.maxDrainUs,
        static_cast<uint32_t>(nowUs_()) - drainStartUs
    );
}

void UsbMidi::sendShortMessage_(const QueuedShortMessage& message) {
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

void UsbMidi::maybeLogOutputQueueStats_() {
    const uint32_t nowMs = millis();
    if (output_stats_.windowStartMs == 0) {
        output_stats_.reset(nowMs);
        return;
    }

    if ((nowMs - output_stats_.windowStartMs) < 1000U) {
        return;
    }

    if (output_stats_.droppedCount > 0 || output_stats_.maxDepth >= 16U || output_stats_.maxDrainUs >= 1000U) {
        OC_LOG_INFO("[Perf][UsbMidiOut] enq={} sent={} drop={} maxDepth={} maxDrain={}us",
                    output_stats_.enqueuedCount,
                    output_stats_.sentCount,
                    output_stats_.droppedCount,
                    output_stats_.maxDepth,
                    output_stats_.maxDrainUs);
    }

    output_stats_.reset(nowMs);
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
