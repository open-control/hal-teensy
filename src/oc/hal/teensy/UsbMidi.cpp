#include "UsbMidi.hpp"

#include <Arduino.h>
#include <algorithm>

#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "detail/UsbMidiTx.h"

#if defined(MS_STORAGE_QUALIFICATION)
#include "QualificationTelemetry.hpp"
#endif

namespace {
void (*midiOutputInterrupt)() = nullptr;
}

// Reserve the SDK AudioStream software IRQ explicitly. AudioStream defines
// this same strong C++ symbol: linking both owners fails instead of silently
// stealing the vector. PendSV/EventResponder and the PIT allocator are untouched.
void software_isr() {
    if (midiOutputInterrupt) midiOutputInterrupt();
}

namespace oc::hal::teensy {

namespace {

using oc::interface::MidiOutputAcceptance;
using oc::realtime::InterruptGuard;

inline uint32_t readPrimask() {
    uint32_t primask = 0;
#ifdef ARDUINO
    asm volatile("MRS %0, primask" : "=r"(primask));
#endif
    return primask;
}

inline uint32_t readIpsr() {
    uint32_t ipsr = 0;
#ifdef ARDUINO
    asm volatile("MRS %0, ipsr" : "=r"(ipsr));
#endif
    return ipsr;
}

uint32_t shortPacket(uint8_t status, uint8_t channel, uint8_t data1, uint8_t data2) {
    return (status >> 4) | (uint32_t(status | channel) << 8) |
           (uint32_t(data1) << 16) | (uint32_t(data2) << 24);
}

}  // namespace

FLASHMEM oc::type::Result<void> UsbMidi::init() {
    if (initialized_) return oc::type::Result<void>::ok();
    InterruptGuard lock;
    if (output_owner_ || NVIC_IS_ENABLED(IRQ_SOFTWARE)) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "USB MIDI output IRQ already owned"});
    }
    output_owner_ = this;
    midiOutputInterrupt = []() {
        if (output_owner_) output_owner_->processOutputInterrupt_();
    };
    attachInterruptVector(IRQ_SOFTWARE, software_isr);
    NVIC_SET_PRIORITY(IRQ_SOFTWARE, OUTPUT_IRQ_PRIORITY);
    NVIC_CLEAR_PENDING(IRQ_SOFTWARE);
    output_session_ = oc_usb_midi_session();
    initialized_ = true;
    oc_usb_midi_set_wakeup(requestOutput_);
    NVIC_ENABLE_IRQ(IRQ_SOFTWARE);
    return oc::type::Result<void>::ok();
}

FLASHMEM UsbMidi::~UsbMidi() {
    InterruptGuard lock;
    if (output_owner_ != this) return;
    NVIC_DISABLE_IRQ(IRQ_SOFTWARE);
    NVIC_CLEAR_PENDING(IRQ_SOFTWARE);
    oc_usb_midi_set_wakeup(nullptr);
    midiOutputInterrupt = nullptr;
    output_owner_ = nullptr;
}

void UsbMidi::requestOutput_() {
#if OC_ENABLE_STATS
    InterruptGuard lock;
    if (output_owner_ && !output_owner_->output_wake_pending_) {
        output_owner_->output_wake_us_ = micros();
        output_owner_->output_wake_pending_ = true;
    }
#endif
    NVIC_SET_PENDING(IRQ_SOFTWARE);
}

void UsbMidi::setOutputRefill(OutputRefill callback, void* context) {
    InterruptGuard lock;
    output_refill_ = callback;
    output_refill_context_ = context;
}

void UsbMidi::update() {
    if (!initialized_) return;

    pollInput();
    serviceOutput();
}

void UsbMidi::pollInput() {
    if (!initialized_) return;
    (void)nowUs_(); // Keep the foreground-owned DWT wrap extender sampled even when input is idle.

    size_t processedCount = 0U;
    while (processedCount < MAX_INPUT_MESSAGES_PER_POLL && usbMIDI.read()) {
        ++processedCount;
#if OC_ENABLE_STATS
        if (received_message_count_ != UINT32_MAX) ++received_message_count_;
#endif
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

    if (processedCount == MAX_INPUT_MESSAGES_PER_POLL) {
        if (input_budget_hit_count_ != UINT32_MAX) {
            ++input_budget_hit_count_;
        }
        OC_PERF_RECORD(
            "midi.usb-input-budget",
            0U,
            static_cast<uint32_t>(processedCount),
            static_cast<uint32_t>(MAX_INPUT_MESSAGES_PER_POLL)
        );
        reportInputBudgetHits_();
    }
    reportOutputRejections_();
}

void UsbMidi::serviceOutput() {
    serviceOutput(DEFAULT_OUTPUT_DRAIN_BUDGET_US);
}

void UsbMidi::serviceOutput(uint32_t budgetUs) {
    if (!initialized_) return;
    // Compatibility polling only requests service. It never becomes another
    // consumer, and cannot impose an unbounded budget on the IRQ owner.
    {
        InterruptGuard lock;
        if (budgetUs && (output_session_ != oc_usb_midi_session() ||
            (!output_blocked_ && (output_queue_count_ || panic_requested_ || panic_pending_))))
            requestOutput_();
    }
    if (readIpsr() == 0U) reportOutputRejections_();
}

void UsbMidi::processOutputInterrupt_() {
    if (!initialized_ || readPrimask() != 0U) return;
#if OC_ENABLE_STATS
    const uint32_t irqStartUs = micros();
    uint32_t wakeAge = 0;
    {
        InterruptGuard lock;
        if (output_wake_pending_) wakeAge = irqStartUs - output_wake_us_;
        output_wake_pending_ = false;
    }
    OC_PERF_RECORD("midi.usb-wake-age", wakeAge, 0U, 0U);
#endif
    output_servicing_ = true;
    output_blocked_ = false;
    reconcileOutputSession_();
    {
        InterruptGuard lock;
        if (panic_requested_) {
            clearOutputQueue_();
            panic_requested_ = false;
            panic_pending_ = true;
            panic_word_ = 0;
        }
    }
#if OC_ENABLE_STATS
    const uint32_t now = micros();
    uint32_t pending = 0U;
    uint32_t highWater = 0U;
    {
        InterruptGuard lock;
        pending = static_cast<uint32_t>(output_queue_count_);
        highWater = static_cast<uint32_t>(output_queue_high_water_);
        output_queue_high_water_ = output_queue_count_;
    }
    if (output_service_seen_) {
        OC_PERF_RECORD("midi.usb-service-gap", now - last_output_service_us_,
                       pending, highWater);
    }
    output_service_seen_ = true;
    last_output_service_us_ = now;
#endif
    drainOutputQueue_(OUTPUT_IRQ_BUDGET_US);
    {
        InterruptGuard lock;
        output_servicing_ = false;
        // Busy/offline waits for DMA retirement or configuration. Never create
        // an interrupt retry storm while an unavailable host owns the buffers.
        if (!output_blocked_ && (output_queue_count_ || panic_pending_ || panic_requested_))
            requestOutput_();
    }
#if OC_ENABLE_STATS
    OC_PERF_RECORD("midi.usb-output-irq", micros() - irqStartUs, 0U, 0U);
#endif
}

MidiOutputAcceptance UsbMidi::sendCC(uint8_t channel, uint8_t cc, uint8_t value) {
    return enqueueShortMessage_(0xB0, channel, cc, value);
}

MidiOutputAcceptance UsbMidi::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    return enqueueShortMessage_(0x90, channel, note, velocity);
}

MidiOutputAcceptance UsbMidi::sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    return enqueueShortMessage_(0x80, channel, note, velocity);
}

MidiOutputAcceptance UsbMidi::sendSysEx(const uint8_t* data, size_t length) {
    if (!initialized_ || !data || length < 2 || length > MAX_SYSEX_BYTES ||
        data[0] != 0xF0 || data[length - 1] != 0xF7) {
        return MidiOutputAcceptance::REJECTED;
    }
    for (size_t i = 1; i + 1 < length; ++i) {
        if (data[i] > 0x7F) return MidiOutputAcceptance::REJECTED;
    }
    InterruptGuard lock;
    if (!canEnqueue_((length + 2) / 3)) return MidiOutputAcceptance::REJECTED;
    for (size_t i = 0; i < length; i += 3) {
        const size_t bytes = std::min<size_t>(3, length - i);
        uint32_t packet = length - i > 3 ? 4 : 4 + bytes;
        for (size_t j = 0; j < bytes; ++j) packet |= uint32_t(data[i + j]) << (8 * (j + 1));
        appendPacket_(packet);
    }
    if (!output_servicing_ && !output_blocked_) requestOutput_();
    return MidiOutputAcceptance::ACCEPTED;
}

MidiOutputAcceptance UsbMidi::sendProgramChange(uint8_t channel, uint8_t program) {
    return enqueueShortMessage_(0xC0, channel, program, 0);
}

MidiOutputAcceptance UsbMidi::sendPitchBend(uint8_t channel, int16_t value) {
    if (value < -8192 || value > 8191) return MidiOutputAcceptance::REJECTED;
    const uint16_t bend = value + 8192;
    return enqueueShortMessage_(0xE0, channel, bend & 0x7F, bend >> 7);
}

MidiOutputAcceptance UsbMidi::sendChannelPressure(uint8_t channel, uint8_t pressure) {
    return enqueueShortMessage_(0xD0, channel, pressure, 0);
}

MidiOutputAcceptance UsbMidi::sendClock() {
    return enqueueShortMessage_(0xF8, 0, 0, 0);
}

MidiOutputAcceptance UsbMidi::sendStart() {
    return enqueueShortMessage_(0xFA, 0, 0, 0);
}

MidiOutputAcceptance UsbMidi::sendStop() {
    return enqueueShortMessage_(0xFC, 0, 0, 0);
}

MidiOutputAcceptance UsbMidi::sendContinue() {
    return enqueueShortMessage_(0xFB, 0, 0, 0);
}

void UsbMidi::allNotesOff() {
    if (!initialized_) return;
    {
        InterruptGuard lock;
        panic_requested_ = true;
        requestOutput_();
    }
}

uint64_t UsbMidi::nowUs_() {
    return clock_.micros64();
}

MidiOutputAcceptance UsbMidi::enqueueShortMessage_(uint8_t status, uint8_t channel,
                                                   uint8_t data1, uint8_t data2) {
    if (!initialized_ || channel > 15 || data1 > 127 || data2 > 127) {
        return MidiOutputAcceptance::REJECTED;
    }
    InterruptGuard lock;
    if (!canEnqueue_(1)) return MidiOutputAcceptance::REJECTED;
    appendPacket_(shortPacket(status, channel, data1, data2));
    if (!output_servicing_ && !output_blocked_) requestOutput_();
    return MidiOutputAcceptance::ACCEPTED;
}

// The producer holds InterruptGuard across capacity check and append(s).
bool UsbMidi::canEnqueue_(size_t count) {
    if (!output_session_ || output_session_ != oc_usb_midi_session() ||
        panic_pending_ || panic_requested_ || count > output_queue_.size() - output_queue_count_) {
        if (rejected_output_count_ != UINT32_MAX) ++rejected_output_count_;
#if defined(MS_STORAGE_QUALIFICATION)
        qualification::noteMidiOutputDrop();
#endif
        return false;
    }

    return true;
}

void UsbMidi::appendPacket_(uint32_t data) {
    output_queue_[output_queue_tail_] = {
        .data = data,
#if OC_ENABLE_STATS
        .admittedUs = micros(),
#endif
    };
    output_queue_tail_ = (output_queue_tail_ + 1U) % output_queue_.size();
    output_queue_count_ += 1U;
#if OC_ENABLE_STATS
    output_queue_high_water_ = std::max(output_queue_high_water_, output_queue_count_);
#endif
}

bool UsbMidi::peekPacket_(QueuedPacket& packet) {
    InterruptGuard lock;
    if (!output_queue_count_) return false;
    packet = output_queue_[output_queue_head_];
    return true;
}

void UsbMidi::clearOutputQueue_() {
    output_queue_head_ = 0;
    output_queue_tail_ = 0;
    output_queue_count_ = 0;
}

void UsbMidi::drainOutputQueue_(uint32_t budgetUs) {
    if (!budgetUs || !output_session_) { output_blocked_ = true; return; }
    // Output uses the SDK's wrap-safe 32-bit clock. The extending input clock
    // remains foreground-owned; do not share its mutable wrap state with IRQs.
    const uint32_t drainStartUs = micros();
    uint32_t sentCount = 0;
    uint32_t attemptedCount = 0;
    bool refilled = false;
    bool moreDue = false;
#if OC_ENABLE_STATS
    uint32_t ageCount = 0;
    uint32_t maxQueueAgeUs = 0;
    uint32_t maxSendUs = 0;
#endif

    while (attemptedCount < OUTPUT_QUEUE_CAPACITY &&
           micros() - drainStartUs < budgetUs) {
        QueuedPacket message;
        bool panic = panic_pending_;
        if (panic && !nextPanicPacket_(message)) panic = false;
        if (!panic && !peekPacket_(message)) {
            if (!refilled && output_refill_) {
                refilled = true;
                moreDue = output_refill_(output_refill_context_, OUTPUT_REFILL_BUDGET_US);
                if (peekPacket_(message)) continue;
            }
            break;
        }
#if OC_ENABLE_STATS
        const uint32_t sendStartUs = micros();
#endif
        const auto result = oc_usb_midi_try_write(message.data, output_session_);
        ++attemptedCount;
#if OC_ENABLE_STATS
        const uint32_t admissionUs = micros() - sendStartUs;
        maxSendUs = std::max(maxSendUs, admissionUs);
#endif
        if (result != OC_USB_MIDI_ACCEPTED) {
            output_blocked_ = true;
            if (result == OC_USB_MIDI_ERROR && output_error_count_ != UINT32_MAX) {
                ++output_error_count_;
            }
            OC_PERF_RECORD("midi.usb-admission-refused", admissionUs, uint32_t(result),
                           uint32_t(output_queue_count_));
            break; // Keep ownership and retry on the next service, never spin.
        }
        if (!panic) {
            InterruptGuard lock;
            output_queue_head_ = (output_queue_head_ + 1U) % output_queue_.size();
            --output_queue_count_;
        }
        acknowledgePacket_(message.data);
        ++sentCount;
#if OC_ENABLE_STATS
        if (!panic) {
            ++ageCount;
            maxQueueAgeUs = std::max(maxQueueAgeUs, sendStartUs - message.admittedUs);
        }
#endif
    }
    if (sentCount) oc_usb_midi_flush(output_session_);
    // A refill can use its budget before emptying the due source. Tail-chain
    // another bounded service, but only after progress and never on USB busy.
    if (!output_blocked_ && (moreDue ||
        (attemptedCount == OUTPUT_QUEUE_CAPACITY && output_refill_))) requestOutput_();
#if OC_ENABLE_STATS
    const uint32_t elapsedUs = micros() - drainStartUs;
    if (attemptedCount) {
        OC_PERF_RECORD("midi.usb-output-drain", elapsedUs, sentCount, budgetUs);
        OC_PERF_RECORD("midi.usb-send-max", maxSendUs, attemptedCount, 0U);
    }
    if (ageCount) {
        // Queue age ends at DMA-buffer admission, not receipt by the host.
        OC_PERF_RECORD("midi.usb-queue-age", maxQueueAgeUs, ageCount, 0U);
    }
#endif
}

void UsbMidi::reconcileOutputSession_() {
    InterruptGuard lock;
    const uint32_t session = oc_usb_midi_session();
    if (session == output_session_) return;
    const auto cancelled = static_cast<uint32_t>(output_queue_count_);
    cancelled_output_count_ += std::min(cancelled, UINT32_MAX - cancelled_output_count_);
    clearOutputQueue_();
    output_session_ = session;
    // A reconfigured endpoint starts a new stream. Do not replay old queued
    // notes; conservatively release notes previously admitted to the old one.
    sysex_open_ = false;
    panic_word_ = 0;
    panic_pending_ = true;
}

bool UsbMidi::nextPanicPacket_(QueuedPacket& packet) {
    if (sysex_open_) {
        packet.data = 0xF705; // Terminate an interrupted SysEx before note-offs.
        return true;
    }
    while (panic_word_ < MIDI_CHANNEL_COUNT * ACTIVE_NOTE_WORD_COUNT) {
        const uint8_t channel = panic_word_ / ACTIVE_NOTE_WORD_COUNT;
        const uint8_t wordIndex = panic_word_ % ACTIVE_NOTE_WORD_COUNT;
        const uint32_t word = active_notes_[channel][wordIndex];
        if (word) {
            const uint8_t note = wordIndex * ACTIVE_NOTE_WORD_BITS + __builtin_ctz(word);
            packet.data = shortPacket(0x80, channel, note, 0);
            return true;
        }
        ++panic_word_;
    }
    InterruptGuard lock;
    panic_pending_ = false;
    return false;
}

void UsbMidi::acknowledgePacket_(uint32_t packet) {
    const uint8_t cin = packet & 0x0F;
    const uint8_t status = (packet >> 8) & 0xFF;
    const uint8_t channel = status & 0x0F;
    const uint8_t note = (packet >> 16) & 0x7F;
    if (cin == 4) sysex_open_ = true;
    else if (cin >= 5 && cin <= 7) sysex_open_ = false;
    else if (cin == 8 || cin == 9) {
        auto& word = active_notes_[channel][note / ACTIVE_NOTE_WORD_BITS];
        const uint32_t bit = uint32_t{1} << (note % ACTIVE_NOTE_WORD_BITS);
        if (cin == 9 && (packet >> 24)) word |= bit;
        else word &= ~bit;
    }
#if defined(MS_STORAGE_QUALIFICATION)
    const auto trafficKind = status == 0xF8
        ? qualification::MidiTrafficKind::Clock
        : (cin == 8
               ? qualification::MidiTrafficKind::NoteOff
               : qualification::MidiTrafficKind::Other);
    qualification::midiOutputPulse(trafficKind);
#endif
}

FLASHMEM void UsbMidi::reportInputBudgetHits_() {
    if (input_budget_hit_count_ == 0U) return;

    const uint32_t nowMs = millis();
    if (last_input_budget_report_ms_ != 0U &&
        (nowMs - last_input_budget_report_ms_) < 1000U) {
        return;
    }

    [[maybe_unused]] const uint32_t hits = input_budget_hit_count_;
    input_budget_hit_count_ = 0U;
    last_input_budget_report_ms_ = nowMs;
    OC_LOG_WARN(
        "UsbMidi input poll reached {}-message budget {} time(s); "
        "additional input, if any, deferred",
        MAX_INPUT_MESSAGES_PER_POLL,
        hits
    );
}

void UsbMidi::reportOutputRejections_() {
    if (!rejected_output_count_ && !cancelled_output_count_ && !output_error_count_) return;

    const uint32_t nowMs = millis();
    if (last_rejection_report_ms_ != 0 &&
        (nowMs - last_rejection_report_ms_) < 1000U) {
        return;
    }

    uint32_t rejected = 0, cancelled = 0, errors = 0;
    {
        InterruptGuard lock;
        rejected = rejected_output_count_;
        rejected_output_count_ = 0;
        cancelled = cancelled_output_count_;
        cancelled_output_count_ = 0;
        errors = output_error_count_;
        output_error_count_ = 0;
    }
    if (rejected > 0) {
        OC_PERF_RECORD("midi.usb-rejections", 0U, rejected, OUTPUT_QUEUE_CAPACITY);
        OC_LOG_WARN("UsbMidi output queue rejected {} message(s)", rejected);
    }
    if (cancelled || errors) {
        OC_PERF_RECORD("midi.usb-session-reset", 0U, cancelled, errors);
        OC_LOG_WARN("UsbMidi session cancelled {} packet(s); DMA admission errors={}",
                    cancelled, errors);
    }
    last_rejection_report_ms_ = nowMs;
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
