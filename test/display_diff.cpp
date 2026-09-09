// Run against the product's ILI9341_T4 checkout, not a reimplementation.
// c++ -std=c++17 -I test/stubs -I <ILI9341_T4>/src test/display_diff.cpp
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstring>

// Only the library's diagnostics need Arduino services in this host test.
struct Stream { template<class... T> void print(T...) {} };
inline Stream Serial;
struct elapsedMicros { operator uint32_t() const { return 0; } };
#include <DiffBuffTemplate.h>

struct Panel {
    static constexpr int LX = 240, LY = 320;
    static constexpr int MAX_WRITE_LINE = 120, MIN_SCANLINE_SPACE = 8;
};
using Diff = T4Diff::DiffBuffT<Panel>;
using Frame = std::array<uint16_t, Panel::LX * Panel::LY>;

static void apply(Diff& diff, const Frame& source, Frame& screen) {
    diff.initRead();
    int x, y, length;
    for (int calls = 0; calls < static_cast<int>(screen.size()); ++calls) {
        const int status = diff.readDiff(x, y, length, Panel::LY + 120);
        if (status < 0) return;
        assert(status == 0 && length > 0);
        const int offset = y * Panel::LX + x;
        assert(offset >= 0 && offset + length <= static_cast<int>(screen.size()));
        std::copy_n(source.begin() + offset, length, screen.begin() + offset);
    }
    assert(false && "diff must terminate");
}

int main() {
    // Dense changes, sparse notes/playheads, unchanged frames, and overflow;
    // consecutive updates must reconstruct exactly the same physical pixels.
    for (int rotation = 0; rotation < 4; ++rotation) {
        for (int capacity : {32, 7680}) {
            for (int inset : {0, 9, 27}) {
                Frame input{}, fast{}, region{}, screen{};
                std::array<uint8_t, 7680> storage{};
                Diff diff(storage.data(), capacity);
                const int width = rotation % 2 ? 320 : 240;
                const int height = rotation % 2 ? 240 : 320;
                for (int pass = 0; pass < 8; ++pass) {
                    const int top = pass == 0 ? 0 : inset;
                    const int bottom = pass == 0 ? height - 1 : height - inset - 1;
                    if (pass % 3 != 2) {
                        for (size_t i = top * width; i < static_cast<size_t>((bottom + 1) * width); ++i) {
                            if (pass == 0 || i % 97 == static_cast<size_t>(pass))
                                input[i] = static_cast<uint16_t>(i * 31 + pass * 127);
                        }
                    }
                    diff.computeDiff(fast.data(), input.data(), rotation, 6, true, 0);
                    apply(diff, fast, screen);
                    diff.computeDiff(region.data(), nullptr, input.data() + top * width,
                        0, width - 1, top, bottom, width, rotation, 6, true, 0);
                    assert(fast == region);
                    assert(screen == region);
                }
            }
        }
    }
}
