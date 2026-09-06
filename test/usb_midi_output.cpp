#include <oc/hal/teensy/UsbMidi.hpp>
#include <oc/hal/teensy/detail/UsbMidiTx.h>
#include <oc/diagnostics/Performance.hpp>

#include <cassert>
#include <vector>

using oc::hal::teensy::UsbMidi;
using oc::interface::MidiOutputAcceptance;
constexpr auto accepted = MidiOutputAcceptance::ACCEPTED;
constexpr auto rejected = MidiOutputAcceptance::REJECTED;

static uint32_t session;
static size_t attempts;
static size_t flushes;
static size_t capacity;
static bool dmaError;
static bool reconnectOnWrite;
static void (*wakeup)();
static uint32_t writeCostUs;
static std::vector<uint32_t> packets;
#if OC_ENABLE_STATS
static std::vector<oc::diagnostics::PerformanceSample> samples;
void oc::diagnostics::recordPerformance(const PerformanceSample& sample) { samples.push_back(sample); }
#endif

uint64_t oc::hal::teensy::HighResolutionClock::micros64() { return ++testMicros; }
extern "C" uint32_t oc_usb_midi_session() { return session; }
extern "C" oc_usb_midi_acceptance oc_usb_midi_try_write(uint32_t packet, uint32_t expected) {
    ++attempts;
    testMicros += writeCostUs;
    if (reconnectOnWrite) { ++session; reconnectOnWrite = false; }
    if (!session || expected != session) return OC_USB_MIDI_OFFLINE;
    if (dmaError) return OC_USB_MIDI_ERROR;
    if (!capacity) return OC_USB_MIDI_BUSY;
    --capacity;
    packets.push_back(packet);
    return OC_USB_MIDI_ACCEPTED;
}
extern "C" void oc_usb_midi_set_wakeup(void (*callback)()) { wakeup = callback; }

static void interruptOnce() {
    if (!testOutputIrqPending) return;
    assert(testOutputIrqEnabled && testOutputIrq);
    testOutputIrqPending = false;
    testOutputIrq();
}
extern "C" void oc_usb_midi_flush(uint32_t expected) {
    assert(expected == session);
    ++flushes;
}

struct Fixture {
    UsbMidi midi;
    Fixture() {
        session = 1; attempts = flushes = 0; capacity = 10000;
        dmaError = reconnectOnWrite = false;
        writeCostUs = 0;
        packets.clear(); testMicros = 0; usbMIDI = {};
#if OC_ENABLE_STATS
        samples.clear();
#endif
        assert(midi.init());
        assert(testOutputIrqPriority == 144);
    }
    void service(uint32_t budgetUs = UsbMidi::DEFAULT_OUTPUT_DRAIN_BUDGET_US) {
        midi.serviceOutput(budgetUs);
        interruptOnce();
    }
};

static void packedMessages() {
    Fixture f;
    assert(f.midi.sendCC(15, 7, 127) == accepted);
    assert(f.midi.sendNoteOn(2, 60, 100) == accepted);
    assert(f.midi.sendNoteOff(2, 60, 40) == accepted);
    assert(f.midi.sendProgramChange(0, 10) == accepted);
    assert(f.midi.sendPitchBend(3, -8192) == accepted);
    assert(f.midi.sendPitchBend(3, 8191) == accepted);
    assert(f.midi.sendChannelPressure(1, 42) == accepted);
    assert(f.midi.sendClock() == accepted);
    assert(f.midi.sendStart() == accepted);
    assert(f.midi.sendStop() == accepted);
    assert(f.midi.sendContinue() == accepted);
    assert(packets.empty());
    f.service();
    assert((packets == std::vector<uint32_t>{0x7F07BF0B, 0x643C9209, 0x283C8208,
        0x000AC00C, 0x0000E30E, 0x7F7FE30E, 0x002AD10D, 0xF80F, 0xFA0F, 0xFC0F, 0xFB0F}));
    assert(flushes == 1);
    assert(f.midi.sendNoteOn(16, 60, 100) == rejected);
    assert(f.midi.sendCC(0, 128, 0) == rejected);
    assert(f.midi.sendNoteOff(0, 0, 128) == rejected);
    assert(f.midi.sendPitchBend(0, 8192) == rejected);
}

static void saturationRetainsOwnership() {
    Fixture f;
    capacity = 0;
    for (size_t i = 0; i < UsbMidi::OUTPUT_QUEUE_CAPACITY; ++i) {
        assert(f.midi.sendCC(0, 1, uint8_t(i)) == accepted);
    }
    assert(f.midi.sendClock() == rejected);
    f.midi.serviceOutput(0);
    assert(attempts == 0);
    for (int i = 0; i < 100; ++i) f.service();
    assert(attempts == 1); // Busy sleeps until USB availability, no IRQ storm.
    assert(packets.empty() && flushes == 0);
    assert(f.midi.sendClock() == rejected); // Still full.
    capacity = 64;
    wakeup();
    f.service();
    assert(packets.size() == 64);
    for (int i = 0; i < 64; ++i) assert(f.midi.sendClock() == accepted);
    capacity = 1000;
    wakeup();
    f.service();
    assert(packets.size() == 192);
    for (uint32_t i = 0; i < 128; ++i) assert(packets[i] == (0x0001B00B | (i << 24)));
    for (size_t i = 128; i < 192; ++i) assert(packets[i] == 0xF80F);
}

static void dmaErrorAndTimeBudget() {
    Fixture f;
    assert(f.midi.sendClock() == accepted);
    dmaError = true;
    f.service();
    assert(attempts == 1 && packets.empty());
    dmaError = false;
    for (int i = 0; i < 10; ++i) assert(f.midi.sendStop() == accepted);
    writeCostUs = 100;
    wakeup();
    f.service(UINT32_MAX); // Foreground cannot override the 250 us IRQ budget.
    assert(packets.size() == 3 && packets[0] == 0xF80F);
    writeCostUs = 0;
    f.service();
    assert(packets.size() == 11 && packets[1] == 0xFC0F);
}

static void sysexIsAtomicAndOwned() {
    Fixture f;
    uint8_t data[] = {0xF0, 1, 2, 3, 4, 5, 0xF7};
    assert(f.midi.sendSysEx(nullptr, 2) == rejected);
    assert(f.midi.sendSysEx(data, sizeof(data) - 1) == rejected);
    for (size_t i = 0; i < UsbMidi::OUTPUT_QUEUE_CAPACITY - 2; ++i) {
        assert(f.midi.sendClock() == accepted);
    }
    assert(f.midi.sendSysEx(data, sizeof(data)) == rejected);
    f.service();
    assert(packets.size() == 126);
    packets.clear();
    assert(f.midi.sendSysEx(data, sizeof(data)) == accepted);
    data[1] = 99; // Caller storage can be reused immediately.
    assert(f.midi.sendClock() == accepted);
    f.service();
    assert((packets == std::vector<uint32_t>{0x0201F004, 0x05040304, 0xF705, 0xF80F}));
    for (size_t length : std::array<size_t, 4>{2, 3, 4, UsbMidi::MAX_SYSEX_BYTES}) {
        std::vector<uint8_t> bytes(length, 1);
        bytes.front() = 0xF0; bytes.back() = 0xF7;
        assert(f.midi.sendSysEx(bytes.data(), bytes.size()) == accepted);
        f.service();
        assert((packets.back() & 0xF) == 5 + ((length - 1) % 3));
    }
    std::vector<uint8_t> tooLarge(UsbMidi::MAX_SYSEX_BYTES + 1, 1);
    tooLarge.front() = 0xF0; tooLarge.back() = 0xF7;
    assert(f.midi.sendSysEx(tooLarge.data(), tooLarge.size()) == rejected);
}

static void panicRetriesAndClosesSysex() {
    Fixture f;
    assert(f.midi.sendNoteOn(15, 127, 100) == accepted);
    f.service();
    packets.clear();
    uint8_t data[] = {0xF0, 1, 2, 3, 0xF7};
    assert(f.midi.sendSysEx(data, sizeof(data)) == accepted);
    capacity = 1;
    f.service(); // First SysEx packet only.
    f.midi.allNotesOff();
    interruptOnce();
    assert(packets.size() == 1);
    assert(f.midi.sendNoteOn(0, 60, 100) == rejected);
    capacity = 1;
    wakeup();
    f.service();
    assert(packets.size() == 2 && packets.back() == 0xF705);
    capacity = 1;
    wakeup();
    f.service();
    assert(packets.size() == 3 && packets.back() == 0x007F8F08);
    capacity = 100;
    wakeup();
    f.service();
    assert(f.midi.sendNoteOn(0, 60, 100) == accepted);
    assert(f.midi.sendNoteOn(0, 60, 0) == accepted);
    f.service();
    const auto beforePanic = packets.size();
    f.midi.allNotesOff();
    interruptOnce();
    assert(packets.size() == beforePanic); // Velocity-zero Note On already released it.
}

static void disconnectAndSessionRaces() {
    Fixture f;
    assert(f.midi.sendNoteOn(0, 60, 100) == accepted);
    f.service();
    assert(f.midi.sendNoteOn(0, 61, 100) == accepted);
    session = 0;
    assert(f.midi.sendClock() == rejected);
    f.service();
    assert(packets.size() == 1);
    session = 2;
    wakeup();
    f.service();
    assert(packets.size() == 2 && packets.back() == 0x003C8008);
    assert(f.midi.sendNoteOn(0, 62, 100) == accepted);
    reconnectOnWrite = true;
    f.service(); // Reconfiguration between peek and DMA admission.
    assert(packets.size() == 2);
    wakeup();
    f.service();
    assert(packets.size() == 2); // Old HAL session packet cancelled, not replayed.
    assert(f.midi.sendClock() == accepted);
    f.service();
    assert(packets.size() == 3 && packets.back() == 0xF80F);
}

static void fullPanicIsBoundedAndComplete() {
    Fixture f;
    for (uint8_t channel = 0; channel < 16; ++channel) {
        for (uint8_t note = 0; note < 128; ++note) {
            assert(f.midi.sendNoteOn(channel, note, 100) == accepted);
        }
        f.service();
    }
    assert(packets.size() == 2048);
    packets.clear();
    f.midi.allNotesOff();
    interruptOnce();
    assert(packets.size() == UsbMidi::OUTPUT_QUEUE_CAPACITY);
    for (int i = 0; i < 16; ++i) f.service();
    assert(packets.size() == 2048);
    for (uint32_t i = 0; i < 2048; ++i) {
        assert(packets[i] == (0x8008 | ((i / 128) << 8) | ((i % 128) << 16)));
    }
    assert(f.midi.sendClock() == accepted);
}

static void inputBudgetStillApplies() {
    Fixture f;
    size_t clocks = 0;
    f.midi.setOnClock([&](uint64_t) { ++clocks; });
    usbMIDI.remaining = 129;
    f.midi.pollInput();
    assert(clocks == 128 && usbMIDI.remaining == 1);
    f.midi.pollInput();
    assert(clocks == 129);
}

static void fullBurstRefillsWithoutForeground() {
    Fixture f;
    struct Source { UsbMidi& midi; uint32_t next = 0; } source{f.midi};
    f.midi.setOutputRefill([](void* context, uint32_t budget) {
        auto& s = *static_cast<Source*>(context);
        assert(budget == UsbMidi::OUTPUT_REFILL_BUDGET_US);
        while (s.next < 832) {
            if (s.midi.sendCC(0, uint8_t(s.next >> 7), uint8_t(s.next & 127)) != accepted)
                return true;
            ++s.next;
        }
        return false;
    }, &source);
    assert(f.midi.sendStart() == accepted);
    // No foreground service, no new timer tick. Only the already-pended IRQs.
    for (int i = 0; i < 30 && testOutputIrqPending; ++i) interruptOnce();
    assert(source.next == 832 && packets.size() == 833 && !testOutputIrqPending);
    for (uint32_t i = 0; i < 832; ++i)
        assert(packets[i + 1] == (0xB00B | ((i >> 7) << 16) | ((i & 127) << 24)));
    f.midi.setOutputRefill(nullptr, nullptr);
    UsbMidi second;
    assert(!second.init()); // Never steal another live owner's vector.
}

int main() {
    packedMessages();
    saturationRetainsOwnership();
    dmaErrorAndTimeBudget();
    sysexIsAtomicAndOwned();
    panicRetriesAndClosesSysex();
    disconnectAndSessionRaces();
    fullPanicIsBoundedAndComplete();
    inputBudgetStillApplies();
    fullBurstRefillsWithoutForeground();
}
