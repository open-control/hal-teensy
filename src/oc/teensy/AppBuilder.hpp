#pragma once

/**
 * @file AppBuilder.hpp
 * @brief Teensy-specific AppBuilder with simplified API
 *
 * @code
 * #include <oc/teensy/Teensy.hpp>
 *
 * std::optional<oc::app::OpenControlApp> app;
 *
 * void setup() {
 *     app = oc::teensy::AppBuilder()
 *         .midi()
 *         .encoders(Config::ENCODERS)
 *         .buttons(Config::BUTTONS)
 *         .inputConfig(Config::INPUT);
 *
 *     app->registerContext<MyContext>(ContextID::MAIN, "Main");
 *     app->begin();
 * }
 * @endcode
 *
 * For custom drivers or mocking, use oc::app::AppBuilder directly.
 */

#include <oc/app/AppBuilder.hpp>
#include <oc/app/OpenControlApp.hpp>
#include <oc/teensy/ButtonController.hpp>
#include <oc/teensy/EncoderController.hpp>
#include <oc/teensy/EncoderToolHardware.hpp>
#include <oc/teensy/TeensyGpio.hpp>
#include <oc/teensy/UsbMidi.hpp>
#include <oc/teensy/UsbSerial.hpp>

#include <Arduino.h>

namespace oc::teensy {

/**
 * @brief Teensy-optimized application builder
 *
 * Wraps oc::app::AppBuilder with convenience methods that:
 * - Auto-configure timeProvider to millis()
 * - Accept hardware definition arrays directly
 * - Create appropriate controllers internally
 * - Support implicit conversion (no .build() needed)
 *
 * For advanced use cases (custom drivers, mocking), use oc::app::AppBuilder directly.
 */
class AppBuilder {
public:
    /**
     * @brief Construct builder with default Teensy time provider
     */
    AppBuilder() {
        builder_.timeProvider(defaultTimeProvider);
    }

    // ═══════════════════════════════════════════════════════════════════
    // MIDI
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Enable USB MIDI output
     * @return Reference to this builder for chaining
     */
    AppBuilder& midi() {
        builder_.midi(std::make_unique<UsbMidi>());
        return *this;
    }

    // ═══════════════════════════════════════════════════════════════════
    // SERIAL
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Enable USB Serial transport with COBS framing
     * @return Reference to this builder for chaining
     */
    AppBuilder& serial() {
        builder_.serial(std::make_unique<UsbSerial>());
        return *this;
    }

    // ═══════════════════════════════════════════════════════════════════
    // ENCODERS
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Configure encoders from definition array
     *
     * @tparam N Number of encoders (auto-deduced)
     * @param defs Array of encoder definitions from Config
     * @return Reference to this builder for chaining
     *
     * @code
     * .encoders(Config::Encoder::ENCODERS)
     * @endcode
     */
    template <size_t N>
    AppBuilder& encoders(const std::array<common::EncoderDef, N>& defs) {
        builder_.encoders(std::make_unique<EncoderController<N>>(defs, encoderFactory()));
        return *this;
    }

    // ═══════════════════════════════════════════════════════════════════
    // BUTTONS
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Configure buttons from definition array
     *
     * @tparam N Number of buttons (auto-deduced)
     * @param defs Array of button definitions from Config
     * @param debounceMs Debounce time in milliseconds (default: 5ms)
     * @return Reference to this builder for chaining
     *
     * @code
     * .buttons(Config::Button::BUTTONS)
     * .buttons(Config::Button::BUTTONS, 10)  // Custom debounce
     * @endcode
     */
    template <size_t N>
    AppBuilder& buttons(const std::array<common::ButtonDef, N>& defs,
                        uint8_t debounceMs = 5) {
        builder_.buttons(std::make_unique<ButtonController<N>>(defs, gpio(), nullptr, debounceMs));
        return *this;
    }

    /**
     * @brief Configure buttons with multiplexer support
     *
     * @tparam N Number of buttons (auto-deduced)
     * @param defs Array of button definitions from Config
     * @param mux Multiplexer for reading muxed buttons
     * @param debounceMs Debounce time in milliseconds (default: 5ms)
     * @return Reference to this builder for chaining
     *
     * @code
     * GenericMux<4> mux(muxConfig, gpio());
     * .buttons(Config::Button::BUTTONS, mux)
     * @endcode
     */
    template <size_t N>
    AppBuilder& buttons(const std::array<common::ButtonDef, N>& defs,
                        hal::IMultiplexer& mux,
                        uint8_t debounceMs = 5) {
        builder_.buttons(std::make_unique<ButtonController<N>>(defs, gpio(), &mux, debounceMs));
        return *this;
    }

    // ═══════════════════════════════════════════════════════════════════
    // CONFIGURATION
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Set input gesture timing configuration
     *
     * @param config Input timing configuration (long press, double tap thresholds)
     * @return Reference to this builder for chaining
     */
    AppBuilder& inputConfig(const core::InputConfig& config) {
        builder_.inputConfig(config);
        return *this;
    }

    // ═══════════════════════════════════════════════════════════════════
    // BUILD
    // ═══════════════════════════════════════════════════════════════════

    /**
     * @brief Implicit conversion to OpenControlApp
     *
     * Enables direct assignment without calling .build():
     * @code
     * std::optional<OpenControlApp> app;
     * app = oc::teensy::AppBuilder()
     *     .midi()
     *     .encoders(Config::ENCODERS);
     * @endcode
     */
    operator app::OpenControlApp() {
        return builder_.build();
    }

private:
    app::AppBuilder builder_;

    static uint32_t defaultTimeProvider() {
        return millis();
    }
};

}  // namespace oc::teensy
