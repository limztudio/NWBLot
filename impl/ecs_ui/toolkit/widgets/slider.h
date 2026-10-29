// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "slider_layout.h"
#include <impl/ecs_ui/toolkit/input/input.adhd.h>


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
    [[nodiscard]] ControlToken controlToken()const noexcept;
    [[nodiscard]] SliderSnapshot snapshot()const noexcept;
    [[nodiscard]] b8 matches(const SliderSnapshot& value)const noexcept;
    [[nodiscard]] b8 setValue(f64 value)noexcept;
    void reset()noexcept;


private:
    void advanceRevision()noexcept;
    void advanceAdmission()noexcept;


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
    [[nodiscard]] static b8 Validate(const SliderOptions& options)noexcept;
    [[nodiscard]] static Expected<f64> Normalize(f64 minimum, f64 maximum, f64 value)noexcept;
    [[nodiscard]] static Expected<f64> Interpolate(f64 minimum, f64 maximum, f64 normalized)noexcept;
    // Admission includes stable geometry and policy, while thumb position and repaint-only value changes preserve its token.
    [[nodiscard]] static b8 Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept;
    [[nodiscard]] static b8 Apply(SliderState& state, SliderResult& result, const SliderOptions& options, const ControlAction& action)noexcept;
    [[nodiscard]] static b8 Seek(SliderState& state, SliderResult& result, const SliderOptions& options, const PointerGesture& gesture)noexcept;
    [[nodiscard]] static b8 Drag(SliderState& state, SliderResult& result, const SliderOptions& options, const PointerGesture& gesture)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

