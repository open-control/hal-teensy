#include <oc/hal/teensy/detail/SerialLogBuffer.hpp>

#include <cassert>
#include <string>

int main() {
    using oc::hal::teensy::detail::SerialLogBuffer;
    SerialLogBuffer buffer;
    std::string output;
    size_t capacity = 0, writes = 0, attempts = 0;
    auto send = [&](const uint8_t* data, size_t size) {
        ++attempts;
        if (size > capacity) return false;
        ++writes;
        capacity -= size;
        output.append(reinterpret_cast<const char*>(data), size);
        return true;
    };
    auto line = [&](const std::string& value) {
        for (const auto c : value) buffer.append(static_cast<uint8_t>(c), 123, send);
    };
    for (int i = 0; i < 10000; ++i) line("unavailable\n");
    assert(writes == 0 && output.empty() && attempts == 10000);
    capacity = 4096;
    line("recovered\n");
    assert(output == "\n[123ms] WARN: [Log] dropped lines=10000\nrecovered\n");
    output.clear();
    line("first part");
    assert(output.empty());
    line(" second part\n");
    assert(output == "first part second part\n");

    output.clear();
    line(std::string(SerialLogBuffer::CAPACITY, 'x') + "\n");
    assert(output.empty());
    line("short\n");
    assert(output.find("dropped lines=1\nshort\n") != std::string::npos);
    output.clear();
    line(std::string(SerialLogBuffer::CAPACITY - 1, 'x') + "\n");
    assert(output.size() == SerialLogBuffer::CAPACITY);

    output.clear();
    capacity = 3;
    line("no partial output\n");
    assert(output.empty());
    buffer.dropLine(); // Rejected ISR line does not touch a foreground fragment.
    line("fragment");
    capacity = 4096;
    line(" complete\n");
    assert(output.find("dropped lines=2\nfragment complete\n") != std::string::npos);
    output.clear();
    line("healthy\n");
    assert(output == "healthy\n");

    // A loss summary may fit while the following line does not. Account for
    // that line exactly once and preserve a rejection arriving during send.
    output.clear();
    buffer.dropLine();
    capacity = std::string("\n[123ms] WARN: [Log] dropped lines=1\n").size();
    line("deferred host capacity\n");
    assert(output == "\n[123ms] WARN: [Log] dropped lines=1\n");
    output.clear();
    capacity = 4096;
    auto interruptedSend = [&](const uint8_t* data, size_t size) {
        if (data[0] == '\n') buffer.dropLine();
        return send(data, size);
    };
    for (const char c : std::string("resumed\n"))
        buffer.append(static_cast<uint8_t>(c), 123, interruptedSend);
    assert(output == "\n[123ms] WARN: [Log] dropped lines=1\nresumed\n");
    output.clear();
    line("after interrupt\n");
    assert(output == "\n[123ms] WARN: [Log] dropped lines=1\nafter interrupt\n");
}
