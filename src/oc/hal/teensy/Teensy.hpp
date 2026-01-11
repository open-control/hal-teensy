#pragma once

/**
 * @file Teensy.hpp
 * @brief Convenience header for Teensy HAL
 *
 * Includes AppBuilder and all hardware drivers for easy setup.
 *
 * @code
 * #include <oc/hal/teensy/Teensy.hpp>
 *
 * app = oc::hal::teensy::AppBuilder()
 *     .midi()
 *     .encoders(Config::ENCODERS)
 *     .buttons(Config::BUTTONS);
 * @endcode
 */

#include <Arduino.h>

// Logging with dependency injection
#include <oc/log/Log.hpp>
#include <oc/hal/teensy/TeensyOutput.hpp>

#include <oc/hal/teensy/AppBuilder.hpp>
#include <oc/hal/teensy/TeensyGpio.hpp>
#include <oc/hal/teensy/EncoderToolHardware.hpp>
#include <oc/hal/teensy/EncoderController.hpp>
#include <oc/hal/teensy/ButtonController.hpp>
#include <oc/hal/teensy/GenericMux.hpp>

namespace oc::hal::teensy {

/**
 * @brief Default time provider using Arduino millis()
 * Used by LVGL bridge and other time-dependent components.
 */
inline uint32_t defaultTimeProvider() {
    return millis();
}

/**
 * @brief Create an encoder controller with default factory
 *
 * @tparam N Number of encoders
 * @param defs Encoder definitions
 * @return Unique pointer to encoder controller
 *
 * @code
 * auto encoders = teensy::makeEncoderController(Config::Enc::ALL);
 * @endcode
 */
template <size_t N>
auto makeEncoderController(const std::array<common::EncoderDef, N>& defs) {
    return std::make_unique<EncoderController<N>>(defs, encoderFactory());
}

/**
 * @brief Create a button controller with default GPIO
 *
 * @tparam N Number of buttons
 * @param defs Button definitions
 * @param mux Optional multiplexer for muxed buttons
 * @param debounceMs Debounce time in milliseconds
 * @return Unique pointer to button controller
 *
 * @code
 * auto buttons = teensy::makeButtonController(Config::Btn::ALL);
 * @endcode
 */
template <size_t N>
auto makeButtonController(
    const std::array<common::ButtonDef, N>& defs,
    hal::IMultiplexer* mux = nullptr,
    uint8_t debounceMs = 5) {
    return std::make_unique<ButtonController<N>>(defs, gpio(), mux, debounceMs);
}

/**
 * @brief Create a multiplexer with default GPIO
 *
 * @tparam NumPins Number of select pins (1-4)
 * @param config Mux configuration
 * @return Unique pointer to multiplexer
 *
 * @code
 * auto mux = teensy::makeMux<4>(muxConfig);
 * @endcode
 */
template <uint8_t NumPins>
auto makeMux(const typename GenericMux<NumPins>::Config& config) {
    return std::make_unique<GenericMux<NumPins>>(config, gpio());
}

}  // namespace oc::hal::teensy
