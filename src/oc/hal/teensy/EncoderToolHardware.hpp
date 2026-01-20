#pragma once

#include <memory>

#include <EncoderTool.h>

#include <oc/types/Result.hpp>
#include <oc/interface/IEncoderHardware.hpp>

namespace oc::hal::teensy {

/**
 * @brief EncoderTool-based encoder hardware (ISR-driven)
 *
 * Uses EncoderTool library for hardware quadrature decoding on Teensy.
 * Callbacks are fired from ISR context.
 */
class EncoderToolHardware : public interface::IEncoderHardware {
public:
    EncoderToolHardware(uint8_t pinA, uint8_t pinB)
        : pinA_(pinA), pinB_(pinB) {}

    oc::Result<void> init() override {
        encoder_.begin(pinA_, pinB_, EncoderTool::CountMode::full);
        if (callback_) {
            encoder_.attachCallback([this](int, int delta) {
                callback_(context_, delta);
            });
        }
        return oc::Result<void>::ok();
    }

    void setDeltaCallback(interface::EncoderDeltaCallback callback, void* context) override {
        callback_ = callback;
        context_ = context;
    }

private:
    uint8_t pinA_;
    uint8_t pinB_;
    EncoderTool::Encoder encoder_;
    interface::EncoderDeltaCallback callback_ = nullptr;
    void* context_ = nullptr;
};

/**
 * @brief Factory for creating EncoderToolHardware instances
 */
class EncoderToolFactory : public interface::IEncoderHardwareFactory {
public:
    std::unique_ptr<interface::IEncoderHardware> create(uint8_t pinA, uint8_t pinB) override {
        return std::make_unique<EncoderToolHardware>(pinA, pinB);
    }
};

/**
 * @brief Global encoder factory instance for convenience
 * @return Reference to singleton EncoderToolFactory
 */
inline EncoderToolFactory& encoderFactory() {
    static EncoderToolFactory instance;
    return instance;
}

}  // namespace oc::hal::teensy
