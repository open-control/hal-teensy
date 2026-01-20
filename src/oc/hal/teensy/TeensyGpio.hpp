#pragma once

#include <Arduino.h>

#include <oc/interface/IGpio.hpp>

namespace oc::hal::teensy {

class TeensyGpio : public interface::IGpio {
public:
    void pinMode(uint8_t pin, interface::PinMode mode) override {
        switch (mode) {
            case interface::PinMode::PIN_INPUT:
                ::pinMode(pin, INPUT);
                break;
            case interface::PinMode::PIN_INPUT_PULLUP:
                ::pinMode(pin, INPUT_PULLUP);
                break;
            case interface::PinMode::PIN_OUTPUT:
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

inline TeensyGpio& gpio() {
    static TeensyGpio instance;
    return instance;
}

}  // namespace oc::hal::teensy
