#pragma once

#include <array>
#include <memory>

#include <oc/hal/common/embedded/EncoderDef.hpp>
#include <oc/type/Result.hpp>
#include <oc/core/input/EncoderLogic.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/interface/IEncoderHardware.hpp>

namespace oc::hal::teensy {

using EncoderDef = common::embedded::EncoderDef;

/**
 * @brief Context passed to ISR callback for encoder identification
 */
template <size_t N>
struct EncoderContext;

/**
 * @brief Teensy encoder controller using hardware abstraction
 *
 * @tparam N Number of encoders to manage
 */
template <size_t N>
class EncoderController : public interface::IEncoder {
public:
    EncoderController(
        const std::array<EncoderDef, N>& defs,
        interface::IEncoderHardwareFactory& factory)
        : defs_(defs), factory_(factory) {
        for (size_t i = 0; i < N; ++i) {
            core::input::EncoderConfig cfg{
                .id = defs[i].id,
                .ppr = defs[i].ppr,
                .rangeAngle = defs[i].rangeAngle,
                .ticksPerEvent = defs[i].ticksPerEvent,
                .invertDirection = defs[i].invertDirection
            };
            encoders_logic_[i] = std::make_unique<core::input::EncoderLogic>(cfg);
            contexts_[i] = {this, i};
        }
    }

    oc::type::Result<void> init() override {
        if (initialized_) return oc::type::Result<void>::ok();

        for (size_t i = 0; i < N; ++i) {
            const auto& def = defs_[i];
            encoders_hw_[i] = factory_.create(def.pinA, def.pinB);
            encoders_hw_[i]->setDeltaCallback(onDelta, &contexts_[i]);
            auto result = encoders_hw_[i]->init();
            if (!result) {
                return result;
            }
        }
        initialized_ = true;
        return oc::type::Result<void>::ok();
    }

    void update() override {
        if (!initialized_) return;
        for (size_t i = 0; i < N; ++i) {
            auto pending = encoders_logic_[i]->flush();
            if (pending.has_value() && callback_) {
                callback_(defs_[i].id, pending.value());
            }
        }
    }

    float getPosition(oc::type::EncoderID id) const override {
        int idx = findIndex(id);
        return idx >= 0 ? encoders_logic_[idx]->getLastValue() : 0.0f;
    }

    void setPosition(oc::type::EncoderID id, float value) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setPosition(value);
    }

    void setMode(oc::type::EncoderID id, interface::EncoderMode mode) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setMode(mode);
    }

    void setBounds(oc::type::EncoderID id, float min, float max) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setBounds(min, max);
    }

    void setDiscreteSteps(oc::type::EncoderID id, uint8_t steps) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setDiscreteSteps(steps);
    }

    void setContinuous(oc::type::EncoderID id) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setContinuous();
    }

    void setDelta(oc::type::EncoderID id, float delta) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setDelta(delta);
    }

    void setCallback(oc::type::EncoderCallback cb) override { callback_ = cb; }

private:
    struct Context {
        EncoderController* controller;
        size_t index;
    };

    static void onDelta(void* ctx, int32_t delta) {
        auto* c = static_cast<Context*>(ctx);
        c->controller->encoders_logic_[c->index]->processDelta(delta);
    }

    int findIndex(oc::type::EncoderID id) const {
        for (size_t i = 0; i < N; ++i) {
            if (defs_[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    std::array<EncoderDef, N> defs_;
    interface::IEncoderHardwareFactory& factory_;
    std::array<Context, N> contexts_;
    std::array<std::unique_ptr<interface::IEncoderHardware>, N> encoders_hw_;
    std::array<std::unique_ptr<core::input::EncoderLogic>, N> encoders_logic_;
    oc::type::EncoderCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
