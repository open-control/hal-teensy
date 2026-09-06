#pragma once

#include <cstddef>
#include <cstdint>

#define FLASHMEM

inline uint32_t testMicros = 0;
inline uint32_t micros() { return testMicros; }
inline uint32_t millis() { return testMicros / 1000; }

// Deliberately no send methods: a HAL output must use checked DMA admission.
struct UsbMidiInputStub {
    enum : uint8_t { ControlChange = 0xB0, NoteOn = 0x90, NoteOff = 0x80,
                    SystemExclusive = 0xF0, Clock = 0xF8, Start = 0xFA,
                    Continue = 0xFB, Stop = 0xFC };
    size_t remaining = 0;
    uint8_t type = Clock;
    bool read() { if (!remaining) return false; --remaining; return true; }
    uint8_t getType() const { return type; }
    uint8_t getChannel() const { return 1; }
    uint8_t getData1() const { return 60; }
    uint8_t getData2() const { return 100; }
    const uint8_t* getSysExArray() const { return nullptr; }
    size_t getSysExArrayLength() const { return 0; }
};
inline UsbMidiInputStub usbMIDI;
