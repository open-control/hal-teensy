#pragma once

#include <Arduino.h>

#include <oc/hal/IGpio.hpp>

namespace oc::teensy {

/**
 * @brief Teensy GPIO implementation using Arduino API
 */
class TeensyGpio : public hal::IGpio {
public:
    void pinMode(uint8_t pin, hal::PinMode mode) override {
        switch (mode) {
            case hal::PinMode::INPUT:
                ::pinMode(pin, INPUT);
                break;
            case hal::PinMode::INPUT_PULLUP:
                ::pinMode(pin, INPUT_PULLUP);
                break;
            case hal::PinMode::OUTPUT:
                ::pinMode(pin, OUTPUT);
                break;
        }
    }

    void digitalWrite(uint8_t pin, bool high) override {
        ::digitalWrite(pin, high ? HIGH : LOW);
    }

    bool digitalRead(uint8_t pin) override {
        return ::digitalRead(pin) == HIGH;
    }

    uint16_t analogRead(uint8_t pin) override {
        return ::analogRead(pin);
    }
};

/**
 * @brief Global GPIO instance for convenience
 * @return Reference to singleton TeensyGpio
 */
inline TeensyGpio& gpio() {
    static TeensyGpio instance;
    return instance;
}

}  // namespace oc::teensy
