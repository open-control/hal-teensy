#include "QualificationTelemetry.hpp"

#if defined(MS_STORAGE_QUALIFICATION)

#include <array>
#include <atomic>
#include <cstddef>

#include <Arduino.h>

namespace oc::hal::teensy::qualification {

namespace {

constexpr uint8_t FOREGROUND_PIN = 7U;
constexpr uint8_t STORAGE_PIN = 9U;
constexpr uint8_t TIMER_PIN = 10U;
constexpr uint8_t MIDI_OUTPUT_PIN = 11U;
constexpr uint8_t DISPLAY_PIN = 12U;
constexpr uint8_t CAPTURE_GATE_PIN = 24U;

enum class Counter : size_t {
    ForegroundTicks = 0,
    StoragePrimitives,
    TimerEntries,
    MidiInputs,
    MidiOutputs,
    ClockInputs,
    ClockOutputs,
    NoteOffInputs,
    NoteOffOutputs,
    MidiOutputDrops,
    DisplaySubmissions,
    Count,
};

using CounterArray = std::array<std::atomic<uint32_t>, static_cast<size_t>(Counter::Count)>;
CounterArray counters{};

static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "qualification counters must remain lock-free");
static_assert(sizeof(CounterArray) == sizeof(TelemetrySnapshot),
              "qualification counters exceed the 44-byte LOCK-S owner");

inline std::atomic<uint32_t>& counter(Counter id) {
    return counters[static_cast<size_t>(id)];
}

inline void increment(Counter id) {
    counter(id).fetch_add(1U, std::memory_order_relaxed);
}

inline uint32_t load(Counter id) {
    return counter(id).load(std::memory_order_relaxed);
}

}  // namespace

FLASHMEM void begin() {
    pinMode(FOREGROUND_PIN, OUTPUT);
    pinMode(STORAGE_PIN, OUTPUT);
    pinMode(TIMER_PIN, OUTPUT);
    pinMode(MIDI_OUTPUT_PIN, OUTPUT);
    pinMode(DISPLAY_PIN, OUTPUT);
    pinMode(CAPTURE_GATE_PIN, INPUT_PULLDOWN);

    digitalWriteFast(FOREGROUND_PIN, LOW);
    digitalWriteFast(STORAGE_PIN, LOW);
    digitalWriteFast(TIMER_PIN, LOW);
    digitalWriteFast(MIDI_OUTPUT_PIN, LOW);
    digitalWriteFast(DISPLAY_PIN, LOW);

    for (auto& value : counters) {
        value.store(0U, std::memory_order_relaxed);
    }
}

bool captureGateActive() {
    return digitalReadFast(CAPTURE_GATE_PIN) != LOW;
}

void foregroundBegin() {
    increment(Counter::ForegroundTicks);
    digitalWriteFast(FOREGROUND_PIN, HIGH);
}

void foregroundEnd() {
    digitalWriteFast(FOREGROUND_PIN, LOW);
}

void storageBegin() {
    increment(Counter::StoragePrimitives);
    digitalWriteFast(STORAGE_PIN, HIGH);
}

void storageEnd() {
    digitalWriteFast(STORAGE_PIN, LOW);
}

void timerPulse() {
    increment(Counter::TimerEntries);
    digitalWriteFast(TIMER_PIN, HIGH);
    digitalWriteFast(TIMER_PIN, LOW);
}

void midiOutputPulse(MidiTrafficKind kind) {
    increment(Counter::MidiOutputs);
    if (kind == MidiTrafficKind::Clock) {
        increment(Counter::ClockOutputs);
    } else if (kind == MidiTrafficKind::NoteOff) {
        increment(Counter::NoteOffOutputs);
    }
    digitalWriteFast(MIDI_OUTPUT_PIN, HIGH);
    digitalWriteFast(MIDI_OUTPUT_PIN, LOW);
}

void displayBegin() {
    increment(Counter::DisplaySubmissions);
    digitalWriteFast(DISPLAY_PIN, HIGH);
}

void displayEnd() {
    digitalWriteFast(DISPLAY_PIN, LOW);
}

void noteMidiInput(MidiTrafficKind kind) {
    increment(Counter::MidiInputs);
    if (kind == MidiTrafficKind::Clock) {
        increment(Counter::ClockInputs);
    } else if (kind == MidiTrafficKind::NoteOff) {
        increment(Counter::NoteOffInputs);
    }
}

void noteMidiOutputDrop() {
    increment(Counter::MidiOutputDrops);
}

FLASHMEM TelemetrySnapshot snapshot() {
    return {
        .foregroundTicks = load(Counter::ForegroundTicks),
        .storagePrimitives = load(Counter::StoragePrimitives),
        .timerEntries = load(Counter::TimerEntries),
        .midiInputs = load(Counter::MidiInputs),
        .midiOutputs = load(Counter::MidiOutputs),
        .clockInputs = load(Counter::ClockInputs),
        .clockOutputs = load(Counter::ClockOutputs),
        .noteOffInputs = load(Counter::NoteOffInputs),
        .noteOffOutputs = load(Counter::NoteOffOutputs),
        .midiOutputDrops = load(Counter::MidiOutputDrops),
        .displaySubmissions = load(Counter::DisplaySubmissions),
    };
}

}  // namespace oc::hal::teensy::qualification

#endif
