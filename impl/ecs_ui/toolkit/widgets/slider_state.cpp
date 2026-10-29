// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <global/simplemath.h>
#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SliderState::SliderState()noexcept
    : m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_slider_state::s_NextIdentity))
    , m_inputGeneration(NextNonWrappingIdentity(__hidden_ui_slider_state::s_NextIdentity))
{}

ControlToken SliderState::controlToken()const noexcept{
    return { m_inputGeneration, m_admissionGeneration, m_instanceGeneration };
}

SliderSnapshot SliderState::snapshot()const noexcept{
    return { .press = m_press, .instanceGeneration = m_instanceGeneration, .inputGeneration = m_inputGeneration, .revision = m_revision, .admissionGeneration = m_admissionGeneration, .valueBits = BitCast<u64>(m_value), .pressMoved = m_pressMoved };
}

b8 SliderState::matches(const SliderSnapshot& value)const noexcept{
    return snapshot() == value;
}

b8 SliderState::setValue(const f64 value)noexcept{
    if(!IsFinite(value))
        return false;
    advanceRevision();
    m_inputGeneration = NextNonWrappingIdentity(__hidden_ui_slider_state::s_NextIdentity);
    m_value = value;
    m_press = {};
    m_pressMoved = false;
    return true;
}

void SliderState::reset()noexcept{
    advanceRevision();
    advanceAdmission();
    m_inputGeneration = NextNonWrappingIdentity(__hidden_ui_slider_state::s_NextIdentity);
    m_value = 0.0;
    m_minimum = 0.0;
    m_maximum = 1.0;
    m_keyStep = 0.0;
    m_admission = {};
    m_press = {};
    m_result = {};
    m_placement = {};
    m_enabled = true;
    m_admitted = false;
    m_pressMoved = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SliderState::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}

void SliderState::advanceAdmission()noexcept{
    if(m_admissionGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_admissionGeneration;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

