#pragma once

#include <array>

#include <oc/hal/common/embedded/ButtonDef.hpp>
#include <oc/hal/common/embedded/GpioPin.hpp>
#include <oc/type/Result.hpp>
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
        uint8_t debounceMs = 5,
        uint8_t muxReadsPerUpdate = 0)
        : buttons_(buttons)
        , gpio_(gpio)
        , mux_(mux)
        , debounce_ms_(debounceMs)
        , mux_reads_per_update_(muxReadsPerUpdate) {
        states_.fill(false);
        last_change_.fill(0);
    }

    oc::type::Result<void> init() override {
        for (const auto& btn : buttons_) {
            if (btn.pin.source == common::embedded::GpioPin::Source::MCU) {
                gpio_.pinMode(btn.pin.pin, interface::PinMode::PIN_INPUT_PULLUP);
            }
        }
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    void update(uint32_t currentTimeMs) override {
        if (!initialized_) return;

        // Direct GPIO is cheap and remains sampled on every app tick.
        for (size_t i = 0; i < N; ++i) {
            if (buttons_[i].pin.source == common::embedded::GpioPin::Source::MCU) {
                sampleButton_(i, currentTimeMs);
            }
        }

        sampleMuxButtons_(currentTimeMs);
    }

    bool isPressed(oc::type::ButtonID id) const override {
        for (size_t i = 0; i < N; ++i) {
            if (buttons_[i].id == id) return states_[i];
        }
        return false;
    }

    void setCallback(oc::type::ButtonCallback cb) override { callback_ = cb; }

private:
    void sampleButton_(size_t index, uint32_t currentTimeMs) {
        const auto& button = buttons_[index];
        const bool raw = readPin(button);
        const bool pressed = button.activeLow ? !raw : raw;

        if (pressed == states_[index] ||
            currentTimeMs - last_change_[index] < debounce_ms_) {
            return;
        }

        states_[index] = pressed;
        last_change_[index] = currentTimeMs;
        if (callback_) {
            callback_(
                button.id,
                pressed ? oc::type::ButtonEvent::PRESSED
                        : oc::type::ButtonEvent::RELEASED
            );
        }
    }

    void sampleMuxButtons_(uint32_t currentTimeMs) {
        if (!mux_) return;

        // A zero budget preserves the historical full-scan behavior. Products
        // with a high app cadence can opt into bounded round-robin sampling.
        if (mux_reads_per_update_ == 0) {
            for (size_t i = 0; i < N; ++i) {
                if (buttons_[i].pin.source == common::embedded::GpioPin::Source::MUX) {
                    sampleButton_(i, currentTimeMs);
                }
            }
            return;
        }

        if constexpr (N > 0) {
            uint8_t sampled = 0;
            for (size_t scanned = 0;
                 scanned < N && sampled < mux_reads_per_update_;
                 ++scanned) {
                const size_t index = next_mux_index_;
                next_mux_index_ = (next_mux_index_ + 1U) % N;
                if (buttons_[index].pin.source != common::embedded::GpioPin::Source::MUX) {
                    continue;
                }
                sampleButton_(index, currentTimeMs);
                ++sampled;
            }
        }
    }

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
    oc::type::ButtonCallback callback_;
    size_t next_mux_index_ = 0;
    uint8_t mux_reads_per_update_ = 0;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
