// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "slider_layout.h"
#include <impl/ecs_ui/toolkit/input/input.adhd.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Public value intents retire copied input; diagnostics are published only after the enclosing Builder scope validates.
class SliderState final : NoCopy{
    friend class Builder;
    friend class SliderBehavior;


public:
    SliderState()noexcept;
    SliderState(SliderState&&) = delete;


public:
    SliderState& operator=(SliderState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 inputGeneration()const noexcept{ return m_inputGeneration; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] u64 admissionGeneration()const noexcept{ return m_admissionGeneration; }

    [[nodiscard]] f64 value()const noexcept{ return m_value; }

    [[nodiscard]] SliderResult result()const noexcept{ return m_result; }

    [[nodiscard]] const SliderPlacement& placement()const noexcept{ return m_placement; }

    [[nodiscard]] ControlToken controlToken()const noexcept{ return { m_inputGeneration, m_admissionGeneration, m_instanceGeneration }; }

    [[nodiscard]] SliderSnapshot snapshot()const noexcept{
        return { .press = m_press, .instanceGeneration = m_instanceGeneration, .inputGeneration = m_inputGeneration, .revision = m_revision, .admissionGeneration = m_admissionGeneration, .valueBits = BitCast<u64>(m_value), .pressMoved = m_pressMoved };
    }

    [[nodiscard]] b8 matches(const SliderSnapshot& value)const noexcept{ return snapshot() == value; }
    [[nodiscard]] b8 setValue(f64 value)noexcept;
    void reset()noexcept;


private:
    void advanceRevision()noexcept{ AdvanceRevision(m_revision); }
    void advanceAdmission()noexcept{ AdvanceRevision(m_admissionGeneration); }


private:
    const u64 m_instanceGeneration;
    u64 m_inputGeneration;
    u64 m_revision = 1u;
    u64 m_admissionGeneration = 1u;
    f64 m_value = 0.0;
    f64 m_minimum = 0.0;
    f64 m_maximum = 1.0;
    f64 m_keyStep = 0.0;
    SliderPlacement m_admission;
    InputActionId m_press;
    SliderResult m_result;
    SliderPlacement m_placement;
    b8 m_enabled = true;
    b8 m_admitted = false;
    b8 m_pressMoved = false;
};

class SliderBehavior final{
private:
    [[nodiscard]] static b8 SameSliderOptions(const SliderState& state, const SliderOptions& options)noexcept{
        return BitCast<u64>(state.m_minimum) == BitCast<u64>(options.minimum)
            && BitCast<u64>(state.m_maximum) == BitCast<u64>(options.maximum)
            && BitCast<u64>(state.m_keyStep) == BitCast<u64>(options.keyStep)
            && state.m_enabled == options.enabled;
    }


public:
    [[nodiscard]] static Expected<f64> Normalize(f64 minimum, f64 maximum, f64 value)noexcept;
    [[nodiscard]] static Expected<f64> Interpolate(f64 minimum, f64 maximum, f64 normalized)noexcept;
    // Admission includes stable geometry and policy, while thumb position and repaint-only value changes preserve its token.
    [[nodiscard]] static b8 Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept;
    [[nodiscard]] static b8 Apply(const SliderOptions& options, const ControlAction& action, SliderState& state, SliderResult& result)noexcept;
    [[nodiscard]] static b8 Seek(const SliderOptions& options, const PointerGesture& gesture, SliderState& state, SliderResult& result)noexcept;
    [[nodiscard]] static b8 Drag(const SliderOptions& options, const PointerGesture& gesture, SliderState& state, SliderResult& result)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

