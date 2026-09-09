#pragma once

#include <cstddef>
#include <cstdint>

#define FLASHMEM

inline uint32_t testMicros = 0;
inline uint32_t micros() { return testMicros; }
inline uint32_t millis() { return testMicros / 1000; }

inline bool testOutputIrqEnabled = false, testOutputIrqPending = false;
inline void (*testOutputIrq)() = nullptr;
inline uint8_t testOutputIrqPriority = 0;
#define IRQ_SOFTWARE 1
#define NVIC_IS_ENABLED(irq) testOutputIrqEnabled
#define NVIC_ENABLE_IRQ(irq) (testOutputIrqEnabled = true)
#define NVIC_DISABLE_IRQ(irq) (testOutputIrqEnabled = false)
#define NVIC_SET_PENDING(irq) (testOutputIrqPending = true)
#define NVIC_CLEAR_PENDING(irq) (testOutputIrqPending = false)
#define NVIC_SET_PRIORITY(irq, priority) (testOutputIrqPriority = priority)
inline void attachInterruptVector(int, void (*callback)()) { testOutputIrq = callback; }

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
