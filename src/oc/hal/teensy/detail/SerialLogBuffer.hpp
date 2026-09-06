#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace oc::hal::teensy::detail {

// DTR is not a receive-readiness contract: the bridge can read with DTR low.
// Also avoid Serial's bool conversion, which calls yield() in the Teensy SDK.
template<typename SerialPort>
bool tryWriteSerialLog(SerialPort& serial, bool configured,
                       const uint8_t* data, size_t size) {
    if (!configured || serial.availableForWrite() < static_cast<int>(size)) return false;
    return serial.write(data, size) == size;
}

// One foreground line, not a backlog. Never send a truncated log on saturation.
// tryWrite must admit the complete line without waiting or return false.
class SerialLogBuffer {
public:
    static constexpr size_t CAPACITY = 512;

    void dropLine() { dropped_.fetch_add(1, std::memory_order_relaxed); }

    template<typename TryWrite>
    void append(uint8_t byte, uint32_t nowMs, TryWrite&& tryWrite) {
        if (size_ < bytes_.size()) bytes_[size_++] = byte;
        else oversized_ = true;
        if (byte != '\n') return;

        if (oversized_) {
            dropLine();
        } else {
            const auto dropped = dropped_.load(std::memory_order_relaxed);
            bool ready = true;
            if (dropped != 0) {
                char warning[96];
                // Initial newline also resynchronizes after a USB disconnect
                // interrupted an otherwise admitted write.
                const int length = std::snprintf(warning, sizeof(warning),
                    "\n[%lums] WARN: [Log] dropped lines=%lu\n",
                    static_cast<unsigned long>(nowMs), static_cast<unsigned long>(dropped));
                ready = tryWrite(reinterpret_cast<const uint8_t*>(warning),
                                 static_cast<size_t>(length));
                if (ready) dropped_.fetch_sub(dropped, std::memory_order_relaxed);
            }
            if (!ready || !tryWrite(bytes_.data(), size_)) dropLine();
        }
        size_ = 0;
        oversized_ = false;
    }

private:
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    std::array<uint8_t, CAPACITY> bytes_{};
    std::atomic<uint32_t> dropped_{0};
    size_t size_ = 0;
    bool oversized_ = false;
};

} // namespace oc::hal::teensy::detail
