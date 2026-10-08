// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"
#include "slider_layout.h"
#include "../input/input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SliderOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    f32 height = 32.0f;
    f64 minimum = 0.0;
    f64 maximum = 1.0;
    // Zero chooses one hundredth of the range, with a representable fallback for tiny spans.
    f64 keyStep = 0.0;
    bool enabled = true;
};

struct SliderResult{
    bool valid = false;
    bool valueChanged = false;
    bool focused = false;
    bool dragging = false;
};

struct SliderSnapshot{
    u64 instanceGeneration = 0u;
    u64 inputGeneration = 0u;
    u64 revision = 0u;
    u64 admissionGeneration = 0u;
    u64 valueBits = 0u;
    InputActionId press;
    bool pressMoved = false;
};

[[nodiscard]] bool operator==(const SliderSnapshot& lhs, const SliderSnapshot& rhs)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Public value intents retire copied input; diagnostics are published only after the enclosing Builder scope validates.
class SliderState final : NoCopy{
    friend class Builder;
    friend class SliderBehavior;


public:
    SliderState()noexcept;


public:
    SliderState(SliderState&&) = delete;
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
    [[nodiscard]] bool matches(const SliderSnapshot& snapshot)const noexcept;
    [[nodiscard]] bool setValue(f64 value)noexcept;
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
    bool m_enabled = true;
    bool m_admitted = false;
    bool m_pressMoved = false;
};

class SliderBehavior final{
public:
    [[nodiscard]] static bool Validate(const SliderOptions& options)noexcept;
    [[nodiscard]] static Expected<f64> Normalize(f64 minimum, f64 maximum, f64 value)noexcept;
    [[nodiscard]] static Expected<f64> Interpolate(f64 minimum, f64 maximum, f64 normalized)noexcept;
    // Admission includes stable geometry and policy, while thumb position and repaint-only value changes preserve its token.
    [[nodiscard]] static bool Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept;
    [[nodiscard]] static bool Apply(
        SliderState& state,
        const SliderOptions& options,
        const ControlAction& action,
        SliderResult& result
    )noexcept;
    [[nodiscard]] static bool Seek(
        SliderState& state,
        const SliderOptions& options,
        const PointerGesture& gesture,
        SliderResult& result
    )noexcept;
    [[nodiscard]] static bool Drag(
        SliderState& state,
        const SliderOptions& options,
        const PointerGesture& gesture,
        SliderResult& result
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

