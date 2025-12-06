#pragma once

#include <array>
#include <memory>

#include <EncoderTool.h>

#include <oc/common/EncoderDef.hpp>
#include <oc/core/input/EncoderLogic.hpp>
#include <oc/hal/IEncoderController.hpp>

namespace oc::teensy {

using EncoderDef = common::EncoderDef;

/**
 * @brief Teensy encoder controller using EncoderTool library (ISR-based)
 *
 * @tparam N Number of encoders to manage
 */
template <size_t N>
class EncoderController : public hal::IEncoderController {
public:
    explicit EncoderController(const std::array<EncoderDef, N>& defs) : defs_(defs) {
        for (size_t i = 0; i < N; ++i) {
            core::input::EncoderConfig cfg{
                .id = defs[i].id,
                .ppr = defs[i].ppr,
                .rangeAngle = defs[i].rangeAngle,
                .ticksPerEvent = defs[i].ticksPerEvent,
                .invertDirection = defs[i].invertDirection
            };
            encoders_logic_[i] = std::make_unique<core::input::EncoderLogic>(cfg);
        }
    }

    bool init() override {
        if (initialized_) return true;

        for (size_t i = 0; i < N; ++i) {
            const auto& def = defs_[i];
            encoders_hw_[i] = std::make_unique<EncoderTool::Encoder>();
            encoders_hw_[i]->begin(def.pinA, def.pinB, EncoderTool::CountMode::full);
            encoders_hw_[i]->attachCallback([this, i](int, int delta) {
                encoders_logic_[i]->processDelta(delta);
            });
        }
        initialized_ = true;
        return true;
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
    int findIndex(hal::EncoderID id) const {
        for (size_t i = 0; i < N; ++i) {
            if (defs_[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    std::array<EncoderDef, N> defs_;
    std::array<std::unique_ptr<EncoderTool::Encoder>, N> encoders_hw_;
    std::array<std::unique_ptr<core::input::EncoderLogic>, N> encoders_logic_;
    hal::EncoderCallback callback_;
    bool initialized_ = false;
};

}  // namespace oc::teensy
