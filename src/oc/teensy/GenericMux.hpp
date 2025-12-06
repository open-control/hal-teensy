#pragma once

#include <array>

#include <Arduino.h>

#include <oc/hal/IMultiplexer.hpp>

namespace oc::teensy {

/**
 * @brief Generic multiplexer driver for CD74HC40xx series
 *
 * @tparam NumPins Number of select pins (1-4)
 */
template <uint8_t NumPins>
class GenericMux : public hal::IMultiplexer {
    static_assert(NumPins >= 1 && NumPins <= 4, "Mux supports 1-4 select pins");

public:
    struct Config {
        std::array<uint8_t, NumPins> selectPins;
        uint8_t signalPin;
        uint16_t settleTimeUs = 20;
        bool signalPullup = true;
    };

    explicit GenericMux(const Config& cfg) : config_(cfg) {}

    bool init() override {
        for (uint8_t pin : config_.selectPins) {
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
        }
        pinMode(config_.signalPin, config_.signalPullup ? INPUT_PULLUP : INPUT);
        current_channel_ = 0;
        initialized_ = true;
        return true;
    }

    uint8_t channelCount() const override { return 1 << NumPins; }

    void select(uint8_t channel) override {
        if (!initialized_ || channel >= channelCount()) return;
        if (channel == current_channel_) return;

        for (uint8_t i = 0; i < NumPins; ++i) {
            digitalWrite(config_.selectPins[i], (channel >> i) & 0x01);
        }
        current_channel_ = channel;
        delayMicroseconds(config_.settleTimeUs);
    }

    bool readDigital(uint8_t channel) override {
        select(channel);
        return digitalRead(config_.signalPin);
    }

    uint16_t readAnalog(uint8_t channel) override {
        select(channel);
        return analogRead(config_.signalPin);
    }

    bool supportsAnalog() const override { return true; }

private:
    Config config_;
    uint8_t current_channel_ = 0;
    bool initialized_ = false;
};

using CD74HC4067 = GenericMux<4>;
using CD74HC4051 = GenericMux<3>;
using CD74HC4052 = GenericMux<2>;

}  // namespace oc::teensy
