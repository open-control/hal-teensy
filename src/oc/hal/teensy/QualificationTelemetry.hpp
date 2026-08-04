#pragma once

#include <cstdint>

namespace oc::hal::teensy::qualification {

enum class MidiTrafficKind : uint8_t {
    Other = 0,
    Clock,
    NoteOff,
};

struct TelemetrySnapshot {
    uint32_t foregroundTicks = 0;
    uint32_t storagePrimitives = 0;
    uint32_t timerEntries = 0;
    uint32_t midiInputs = 0;
    uint32_t midiOutputs = 0;
    uint32_t clockInputs = 0;
    uint32_t clockOutputs = 0;
    uint32_t noteOffInputs = 0;
    uint32_t noteOffOutputs = 0;
    uint32_t midiOutputDrops = 0;
    uint32_t displaySubmissions = 0;
};

static_assert(sizeof(TelemetrySnapshot) == 44U, "qualification telemetry ABI drift");

#if defined(MS_STORAGE_QUALIFICATION)

void begin();
bool captureGateActive();

void foregroundBegin();
void foregroundEnd();
void storageBegin();
void storageEnd();
void timerPulse();
void midiOutputPulse(MidiTrafficKind kind);
void displayBegin();
void displayEnd();

void noteMidiInput(MidiTrafficKind kind);
void noteMidiOutputDrop();
TelemetrySnapshot snapshot();

#else

inline void begin() {}
inline bool captureGateActive() { return false; }

inline void foregroundBegin() {}
inline void foregroundEnd() {}
inline void storageBegin() {}
inline void storageEnd() {}
inline void timerPulse() {}
inline void midiOutputPulse(MidiTrafficKind) {}
inline void displayBegin() {}
inline void displayEnd() {}

inline void noteMidiInput(MidiTrafficKind) {}
inline void noteMidiOutputDrop() {}
inline TelemetrySnapshot snapshot() { return {}; }

#endif

}  // namespace oc::hal::teensy::qualification
