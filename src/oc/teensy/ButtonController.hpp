#pragma once

#include <array>

#include <Arduino.h>

#include <oc/common/Types.hpp>
#include <oc/common/ButtonDef.hpp>
#include <oc/hal/IButtonController.hpp>
#include <oc/hal/IMultiplexer.hpp>

namespace oc::teensy {

template <size_t N>
class ButtonController : public hal::IButtonController {
public:
    using ButtonDef = common::ButtonDef;

    ButtonController(
        const std::array<ButtonDef, N>& buttons,
        hal::IMultiplexer* mux = nullptr,
        uint8_t debounceMs = 5)
        : buttons_(buttons), mux_(mux), debounce_ms_(debounceMs) {
        states_.fill(false);
        last_change_.fill(0);
    }

    bool init() override {
        for (const auto& btn : buttons_) {
            if (btn.pin.source == common::GpioPin::Source::MCU) {
                pinMode(btn.pin.pin, INPUT_PULLUP);
            }
        }
        initialized_ = true;
        return true;
    }

    void update() override {
        if (!initialized_) return;

        uint32_t now = millis();

        for (size_t i = 0; i < N; ++i) {
            bool raw = readPin(buttons_[i]);
            bool pressed = buttons_[i].activeLow ? !raw : raw;

            if (pressed != states_[i]) {
                if (now - last_change_[i] >= debounce_ms_) {
                    states_[i] = pressed;
                    last_change_[i] = now;

                    if (callback_) {
                        callback_(
                            buttons_[i].id,
                            pressed ? hal::ButtonEvent::PRESSED
                                    : hal::ButtonEvent::RELEASED);
                    }
                }
            }
        }
    }

    bool isPressed(hal::ButtonID id) const override {
        for (size_t i = 0; i < N; ++i) {
            if (buttons_[i].id == id) return states_[i];
        }
        return false;
    }

    void setCallback(hal::ButtonCallback cb) override { callback_ = cb; }

private:
    bool readPin(const ButtonDef& btn) {
        if (btn.pin.source == common::GpioPin::Source::MCU) {
            return digitalRead(btn.pin.pin);
        } else {
            if (mux_) {
                return mux_->readDigital(btn.pin.pin);
            }
            return false;
        }
    }

    std::array<ButtonDef, N> buttons_;
    hal::IMultiplexer* mux_;
    uint8_t debounce_ms_;

    std::array<bool, N> states_;
    std::array<uint32_t, N> last_change_;
    hal::ButtonCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::teensy
