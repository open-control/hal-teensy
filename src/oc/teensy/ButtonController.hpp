#pragma once

#include <array>

#include <oc/common/Types.hpp>
#include <oc/common/ButtonDef.hpp>
#include <oc/hal/IGpio.hpp>
#include <oc/hal/IButtonController.hpp>
#include <oc/hal/IMultiplexer.hpp>

namespace oc::teensy {

template <size_t N>
class ButtonController : public oc::hal::IButtonController {
public:
    using ButtonDef = common::ButtonDef;

    ButtonController(
        const std::array<ButtonDef, N>& buttons,
        oc::hal::IGpio& gpio,
        oc::hal::IMultiplexer* mux = nullptr,
        uint8_t debounceMs = 5)
        : buttons_(buttons), gpio_(gpio), mux_(mux), debounce_ms_(debounceMs) {
        states_.fill(false);
        last_change_.fill(0);
    }

    bool init() override {
        for (const auto& btn : buttons_) {
            if (btn.pin.source == oc::hal::GpioPin::Source::MCU) {
                gpio_.pinMode(btn.pin.pin, oc::hal::PinMode::INPUT_PULLUP);
            }
        }
        initialized_ = true;
        return true;
    }

    void update(uint32_t currentTimeMs) override {
        if (!initialized_) return;

        for (size_t i = 0; i < N; ++i) {
            bool raw = readPin(buttons_[i]);
            bool pressed = buttons_[i].activeLow ? !raw : raw;

            if (pressed != states_[i]) {
                if (currentTimeMs - last_change_[i] >= debounce_ms_) {
                    states_[i] = pressed;
                    last_change_[i] = currentTimeMs;

                    if (callback_) {
                        callback_(
                            buttons_[i].id,
                            pressed ? oc::hal::ButtonEvent::PRESSED
                                    : oc::hal::ButtonEvent::RELEASED);
                    }
                }
            }
        }
    }

    bool isPressed(oc::hal::ButtonID id) const override {
        for (size_t i = 0; i < N; ++i) {
            if (buttons_[i].id == id) return states_[i];
        }
        return false;
    }

    void setCallback(oc::hal::ButtonCallback cb) override { callback_ = cb; }

private:
    bool readPin(const ButtonDef& btn) {
        if (btn.pin.source == oc::hal::GpioPin::Source::MCU) {
            return gpio_.digitalRead(btn.pin.pin);
        } else {
            if (mux_) {
                return mux_->readDigital(btn.pin.pin);
            }
            return false;
        }
    }

    std::array<ButtonDef, N> buttons_;
    oc::hal::IGpio& gpio_;
    oc::hal::IMultiplexer* mux_;
    uint8_t debounce_ms_;

    std::array<bool, N> states_;
    std::array<uint32_t, N> last_change_;
    oc::hal::ButtonCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::teensy
