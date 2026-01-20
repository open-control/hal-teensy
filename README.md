# Open Control - Teensy HAL

**Hardware Abstraction Layer for Teensy 4.x**

[![Version](https://img.shields.io/badge/version-0.1.0--alpha-blue)]()
[![License](https://img.shields.io/badge/license-Apache--2.0-green)]()

---

## Features

- **USB MIDI** - Native Teensy USB MIDI support
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

## API Reference

### AppBuilder Methods

| Method | Description |
|--------|-------------|
| `.midi()` | Enable USB MIDI output |
| `.encoders(array)` | Configure encoders from definition array |
| `.buttons(array, debounceMs)` | Configure buttons (default 5ms debounce) |
| `.buttons(array, mux, debounceMs)` | Configure buttons with multiplexer |
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

## License

[Apache License 2.0](LICENSE)
