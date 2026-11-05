// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/slider_gesture.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>

#include <global/numeric_range.h>
#include <global/atomic_identity.h>
#include <global/bit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Public value intents retire copied input; diagnostics are published only after the enclosing Builder scope validates.
class SliderState final : NoCopy{
    friend class Builder;
    friend class SliderBehavior;


private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    SliderState()noexcept
        : m_instanceGeneration(NextIdentity())
        , m_inputGeneration(NextIdentity())
    {}
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
    [[nodiscard]] b8 setValue(const f64 value)noexcept{
        if(!IsFinite(value))
            return false;
        AdvanceNonWrappingRevision(m_revision);
        m_inputGeneration = NextIdentity();
        m_value = value;
        m_press = {};
        m_pressMoved = false;
        return true;
    }
    void reset()noexcept{
        AdvanceNonWrappingRevision(m_revision);
        AdvanceNonWrappingRevision(m_admissionGeneration);
        m_inputGeneration = NextIdentity();
        ResetSliderStateFields(
            m_value,
            m_minimum,
            m_maximum,
            m_keyStep,
            m_admission,
            m_press,
            m_result,
            m_placement,
            m_enabled,
            m_admitted,
            m_pressMoved
        );
    }


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
public:
    [[nodiscard]] static Expected<f64> Normalize(const f64 minimum, const f64 maximum, const f64 value)noexcept{
        return NormalizeFiniteRange(minimum, maximum, value);
    }
    [[nodiscard]] static Expected<f64> Interpolate(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
        return InterpolateFiniteRange(minimum, maximum, normalized);
    }
    // Admission includes stable geometry and policy, while thumb position and repaint-only value changes preserve its token.
    [[nodiscard]] static b8 Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept{
        const auto changed = TryEvaluateSliderAdmission(state.m_admitted, state.m_minimum, state.m_maximum, state.m_keyStep, state.m_enabled, state.m_admission, options, placement);
        if(!changed)
            return false;
        if(!*changed)
            return true;
        AdvanceNonWrappingRevision(state.m_revision);
        AdvanceNonWrappingRevision(state.m_admissionGeneration);
        CommitSliderAdmissionState(
            options,
            placement,
            state.m_minimum,
            state.m_maximum,
            state.m_keyStep,
            state.m_enabled,
            state.m_admission,
            state.m_admitted,
            state.m_press,
            state.m_pressMoved
        );
        return true;
    }
    [[nodiscard]] static b8 Apply(const SliderOptions& options, const ControlAction& action, SliderState& state, SliderResult& result)noexcept{
        f64 value = state.m_value;
        b8 advanceRevision = false;
        if(!TryApplySliderControlAction(state.m_admitted, state.m_minimum, state.m_maximum, state.m_keyStep, state.m_enabled, state.controlToken(), state.m_value, options, action, value, advanceRevision, result))
            return false;
        if(advanceRevision){
            AdvanceNonWrappingRevision(state.m_revision);
            state.m_value = value;
        }
        return true;
    }
    [[nodiscard]] static b8 Seek(const SliderOptions& options, const PointerGesture& gesture, SliderState& state, SliderResult& result)noexcept{
        f64 value = state.m_value;
        InputActionId press{};
        b8 advanceRevision = false;
        if(!TrySeekSliderPointerGesture(state.m_admitted, state.m_minimum, state.m_maximum, state.m_keyStep, state.m_enabled, state.controlToken(), state.m_admission, state.m_value, options, gesture, value, press, advanceRevision, result))
            return false;
        if(advanceRevision){
            AdvanceNonWrappingRevision(state.m_revision);
            state.m_value = value;
            state.m_press = press;
            state.m_pressMoved = false;
        }
        return true;
    }
    [[nodiscard]] static b8 Drag(const SliderOptions& options, const PointerGesture& gesture, SliderState& state, SliderResult& result)noexcept{
        f64 value = state.m_value;
        InputActionId press = state.m_press;
        b8 pressMoved = state.m_pressMoved;
        b8 advanceRevision = false;
        if(!TryDragSliderPointerGesture(state.m_admitted, state.m_minimum, state.m_maximum, state.m_keyStep, state.m_enabled, state.controlToken(), state.m_admission, state.m_value, state.m_press, state.m_pressMoved, options, gesture, value, press, pressMoved, advanceRevision, result))
            return false;
        if(advanceRevision){
            AdvanceNonWrappingRevision(state.m_revision);
            state.m_value = value;
            state.m_press = press;
            state.m_pressMoved = pressMoved;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
