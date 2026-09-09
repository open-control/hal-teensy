# Open Control - Teensy HAL

**Hardware Abstraction Layer for Teensy 4.x**

[![Version](https://img.shields.io/badge/version-0.1.0--alpha-blue)]()
[![License](https://img.shields.io/badge/license-Apache--2.0-green)]()

---

## Features

- **USB MIDI** - Native Teensy USB MIDI support
- **SD Filesystem** - Built-in Teensy 4.1 SDIO filesystem backend for named files and directories
- **Rotary Encoders** - Via [EncoderTool](https://github.com/luni64/EncoderTool) with hardware interrupts
- **Button Input** - Debounced GPIO with multiplexer support
- **ILI9341 Display** - DMA-accelerated via [ILI9341_T4](https://github.com/vindar/ILI9341_T4)

---

## Simplified AppBuilder

`oc::teensy::AppBuilder` provides a streamlined API for Teensy projects:

```cpp
#include <oc/teensy/Teensy.hpp>
#include <optional>

std::optional<oc::app::OpenControlApp> app;

void setup() {
    app = oc::teensy::AppBuilder()
        .midi()
        .encoders(Config::ENCODERS)
        .buttons(Config::BUTTONS)
        .inputConfig(Config::INPUT);

    app->registerContext<MyContext>(ContextID::MAIN, "Main");
    app->begin();
}

void loop() {
    app->update();
}
```

### Compared to Generic API

| Feature | `oc::teensy::AppBuilder` | `oc::app::AppBuilder` |
|---------|--------------------------|----------------------|
| Time provider | Auto (`millis()`) | Manual |
| Driver creation | Auto from config arrays | Manual `make_unique` |
| `.build()` call | Not needed (implicit) | Required |
| Use case | Teensy projects | Custom/testing |

---

## Hardware Configuration

Define hardware in `Config.hpp` using designated initializers:

```cpp
#include <oc/common/EncoderDef.hpp>
#include <oc/common/ButtonDef.hpp>

namespace Config {

constexpr std::array<oc::common::EncoderDef, 4> ENCODERS = {{
    {.id = 1, .pinA = 22, .pinB = 23, .ppr = 24, .ticksPerEvent = 4},
    {.id = 2, .pinA = 18, .pinB = 19, .ppr = 24, .ticksPerEvent = 4},
    // ...
}};

constexpr std::array<oc::common::ButtonDef, 2> BUTTONS = {{
    {.id = 1, .pin = {.pin = 32, .source = oc::hal::common::embedded::GpioPin::Source::MCU}},
    {.id = 2, .pin = {.pin = 35, .source = oc::hal::common::embedded::GpioPin::Source::MCU}},
}};

}
```

---

## USB MIDI output contract

`UsbMidi` owns a fixed 128-packet FIFO (512 bytes, or 1024 with admission-age
diagnostics). All output, including SysEx and panic note-offs, passes through
one software IRQ owner. Producers enqueue and wake it; `serviceOutput()` only
requests service and reports diagnostics from the foreground.

- The driver exclusively reserves `IRQ_SOFTWARE`, priority 144: below the
  musical timer/USB (128), above display DMA (160). It uses neither a PIT timer
  nor PendSV. A second instance fails initialization; linking SDK AudioStream
  with this owner fails on their shared strong `software_isr` symbol.
- Each interrupt attempts at most 128 packets with a 250 us cooperative budget.
  The final admission/flush, interrupt preemption and refill transaction can
  exceed that budget; this is not a hard real-time execution bound.
- Optional `setOutputRefill()` supplies already-due upstream work after FIFO
  space becomes available. The callback receives a 40 us cooperative budget,
  must serialize its source and must not allocate, render or access storage.
  It returns whether due work remains. Detach it before destroying that source.

- `ACCEPTED` means copied into the FIFO, **not received by the host**.
- DMA busy/error keeps the head packet for the next service. There is no SDK
  host-wait loop. A busy endpoint sleeps until DMA retirement/reconfiguration
  wakes it, rather than continuously retrying an unavailable host.
- SysEx must include F0/F7 and fit atomically (at most 384 bytes when empty).
  A rejected message is not partially sent and its pointer is never retained.
- `allNotesOff()` cancels pending output and starts bounded panic service.
  The IRQ advances panic without foreground polling. New messages are rejected
  during panic; active-note bits clear only after note-off admission. An already
  started SysEx is terminated before panic note-offs.
- USB reconfiguration cancels the old **HAL** FIFO, reports that cancellation
  and conservatively releases previously admitted notes. Queue owners upstream
  still need their own transport-reset policy; this is not an end-to-end session
  guarantee. Disconnection can invalidate DMA data already accepted by USB.

### SDK build integration

The SDK provides no non-blocking admission result. `script/usb_midi_sdk.py` is a
PlatformIO **PRE** hook that extends its original `usb_midi.c` in one generated
translation unit, reusing the four DMA buffers and descriptors. No SDK package
file is edited or copied into this repository. The hook verifies the reviewed
Teensyduino 1.62 hashes of `usb_midi.c` and `usb.c` and fails on drift; review/requalify it when upgrading
the SDK. Completion callbacks retire descriptors before reuse; SOF and explicit
flush share that bookkeeping. MTP is not qualified. Direct `usbMIDI.send*` calls
must not be mixed with this owner.

This repository's PlatformIO environments install the hook directly. Consumers
must load it before Arduino's build (before ordinary library extra scripts).
Core's `script/pio/teensy_usb_midi.py` installs/resolves the selected HAL with
PlatformIO's package manager and invokes its hook, including on a fresh checkout.
Historical release pins without the new HAL keep their pinned implementation;
updating a pin picks up the extension automatically. Do not claim that an older
pinned release has the new transport.

Output service no longer depends on foreground UI progress. This alone does
not isolate a foreground producer, incoming MIDI clock, transport transitions,
or long interrupt-masked sections. It does not guarantee host USB scheduling or
physical MIDI jitter. `midi.usb-queue-age` ends at SDK buffer admission;
`midi.usb-wake-age` measures pending IRQ delay. The now event-driven
`midi.usb-service-gap` includes idle periods and is **not** an output-delay metric.

---

## API Reference

### AppBuilder Methods

| Method | Description |
|--------|-------------|
| `.midi()` | Enable USB MIDI output |
| `.encoders(array)` | Configure encoders from definition array |
| `.buttons(array, debounceMs)` | Configure buttons (default 5ms debounce) |
| `.buttons(array, mux, debounceMs, muxReadsPerUpdate)` | Configure multiplexed buttons; a zero read budget keeps full-scan behavior |
| `.inputConfig(config)` | Set gesture timing (long press, double tap) |

### Implicit Conversion

No `.build()` needed - assigns directly to `std::optional<OpenControlApp>`:

```cpp
app = oc::teensy::AppBuilder()
    .midi()
    .encoders(Config::ENCODERS);  // implicit conversion
```

---

## Multiplexer Support

For buttons connected via multiplexer:

```cpp
#include <oc/teensy/GenericMux.hpp>

// Create mux
auto mux = oc::teensy::makeMux<4>(muxConfig);

// Pass to builder
app = oc::teensy::AppBuilder()
    .buttons(Config::BUTTONS, *mux);
```

---

## Examples

- [example-teensy41-minimal](https://github.com/open-control/example-teensy41-minimal) - Headless MIDI controller
- [example-teensy41-lvgl](https://github.com/open-control/example-teensy41-lvgl) - With ILI9341 display

---

## SD Filesystem Smoke Test

`oc::hal::teensy::SDFileSystemBackend` implements the OpenControl `IFileSystem`
contract on the Teensy 4.1 built-in SD slot. The backend is allocation-free:
callers provide transfer buffers and directory listing streams entries through
a visitor callback.

The hardware smoke test is opt-in and disabled in normal builds. It creates a
temporary `/oc-fs-smoke` directory, writes and reads a binary payload with CRC,
lists the directory, renames the file, then removes the directory.

```powershell
$env:PLATFORMIO_BUILD_FLAGS="-DOC_HAL_TEENSY_SD_FILESYSTEM_SMOKE"
pio run -e dev -t upload
pio device monitor -b 115200
Remove-Item Env:\PLATFORMIO_BUILD_FLAGS
```

Expected monitor tail:

```text
[oc-fs-smoke] OK bytes=24 crc=0x...
```

---

## License

[Apache License 2.0](LICENSE)
