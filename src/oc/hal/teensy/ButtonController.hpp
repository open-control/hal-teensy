#pragma once

#include <array>

#include <oc/hal/common/embedded/ButtonDef.hpp>
#include <oc/hal/common/embedded/GpioPin.hpp>
#include <oc/types/Result.hpp>
#include <oc/interface/IButton.hpp>
#include <oc/interface/IGpio.hpp>
#include <oc/interface/IMultiplexer.hpp>

namespace oc::hal::teensy {

template <size_t N>
class ButtonController : public interface::IButton {
public:
    using ButtonDef = common::embedded::ButtonDef;

    ButtonController(
        const std::array<ButtonDef, N>& buttons,
        interface::IGpio& gpio,
        interface::IMultiplexer* mux = nullptr,
        uint8_t debounceMs = 5)
        : buttons_(buttons), gpio_(gpio), mux_(mux), debounce_ms_(debounceMs) {
        states_.fill(false);
        last_change_.fill(0);
    }

    oc::Result<void> init() override {
        for (const auto& btn : buttons_) {
            if (btn.pin.source == common::embedded::GpioPin::Source::MCU) {
                gpio_.pinMode(btn.pin.pin, interface::PinMode::PIN_INPUT_PULLUP);
            }
        }
        initialized_ = true;
        return oc::Result<void>::ok();
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
                            pressed ? oc::ButtonEvent::PRESSED
                                    : oc::ButtonEvent::RELEASED);
                    }
                }
            }
        }
    }

    bool isPressed(oc::ButtonID id) const override {
        for (size_t i = 0; i < N; ++i) {
            if (buttons_[i].id == id) return states_[i];
        }
        return false;
    }

    void setCallback(oc::ButtonCallback cb) override { callback_ = cb; }

private:
    bool readPin(const ButtonDef& btn) {
        if (btn.pin.source == common::embedded::GpioPin::Source::MCU) {
            return gpio_.digitalRead(btn.pin.pin);
        } else {
            if (mux_) {
                return mux_->readDigital(btn.pin.pin);
            }
            return false;
        }
    }

    std::array<ButtonDef, N> buttons_;
    interface::IGpio& gpio_;
    interface::IMultiplexer* mux_;
    uint8_t debounce_ms_;

    std::array<bool, N> states_;
    std::array<uint32_t, N> last_change_;
    oc::ButtonCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
