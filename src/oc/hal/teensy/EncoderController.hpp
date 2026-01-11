#pragma once

#include <array>
#include <memory>

#include <oc/hal/common/EncoderDef.hpp>
#include <oc/core/Result.hpp>
#include <oc/core/input/EncoderLogic.hpp>
#include <oc/hal/IEncoderController.hpp>
#include <oc/hal/IEncoderHardware.hpp>

namespace oc::hal::teensy {

using EncoderDef = common::EncoderDef;

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
class EncoderController : public hal::IEncoderController {
public:
    EncoderController(
        const std::array<EncoderDef, N>& defs,
        hal::IEncoderHardwareFactory& factory)
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

    core::Result<void> init() override {
        if (initialized_) return core::Result<void>::ok();

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
        return core::Result<void>::ok();
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

    float getPosition(hal::EncoderID id) const override {
        int idx = findIndex(id);
        return idx >= 0 ? encoders_logic_[idx]->getLastValue() : 0.0f;
    }

    void setPosition(hal::EncoderID id, float value) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setPosition(value);
    }

    void setMode(hal::EncoderID id, hal::EncoderMode mode) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setMode(mode);
    }

    void setBounds(hal::EncoderID id, float min, float max) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setBounds(min, max);
    }

    void setDiscreteSteps(hal::EncoderID id, uint8_t steps) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setDiscreteSteps(steps);
    }

    void setContinuous(hal::EncoderID id) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setContinuous();
    }

    void setDelta(hal::EncoderID id, float delta) override {
        int idx = findIndex(id);
        if (idx >= 0) encoders_logic_[idx]->setDelta(delta);
    }

    void setCallback(hal::EncoderCallback cb) override { callback_ = cb; }

private:
    struct Context {
        EncoderController* controller;
        size_t index;
    };

    static void onDelta(void* ctx, int32_t delta) {
        auto* c = static_cast<Context*>(ctx);
        c->controller->encoders_logic_[c->index]->processDelta(delta);
    }

    int findIndex(hal::EncoderID id) const {
        for (size_t i = 0; i < N; ++i) {
            if (defs_[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    std::array<EncoderDef, N> defs_;
    hal::IEncoderHardwareFactory& factory_;
    std::array<Context, N> contexts_;
    std::array<std::unique_ptr<hal::IEncoderHardware>, N> encoders_hw_;
    std::array<std::unique_ptr<core::input::EncoderLogic>, N> encoders_logic_;
    hal::EncoderCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::hal::teensy
