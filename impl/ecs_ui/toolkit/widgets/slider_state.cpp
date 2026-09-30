// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 NextIdentity(){
    static Atomic<u64> s_NextIdentity{ 1u };
    const u64 identity = s_NextIdentity.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u || identity == Limit<u64>::s_Max)
        TerminateInvariant();
    return identity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool operator==(const SliderSnapshot& lhs, const SliderSnapshot& rhs){
    return
        lhs.instanceGeneration == rhs.instanceGeneration && lhs.inputGeneration == rhs.inputGeneration
        && lhs.revision == rhs.revision && lhs.admissionGeneration == rhs.admissionGeneration && lhs.valueBits == rhs.valueBits
        && lhs.press.target == rhs.press.target && lhs.press.declarationGeneration == rhs.press.declarationGeneration
        && lhs.press.layoutGeneration == rhs.press.layoutGeneration && lhs.press.sequence == rhs.press.sequence
        && lhs.pressMoved == rhs.pressMoved
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SliderState::SliderState()
    : m_instanceGeneration(__hidden_ui_slider_state::NextIdentity())
    , m_inputGeneration(__hidden_ui_slider_state::NextIdentity())
{}

ControlToken SliderState::controlToken()const{
    return { m_inputGeneration, m_admissionGeneration, m_instanceGeneration };
}

SliderSnapshot SliderState::snapshot()const{
    return {
        .instanceGeneration = m_instanceGeneration,
        .inputGeneration = m_inputGeneration,
        .revision = m_revision,
        .admissionGeneration = m_admissionGeneration,
        .valueBits = BitCast<u64>(m_value),
        .press = m_press,
        .pressMoved = m_pressMoved
    };
}

bool SliderState::matches(const SliderSnapshot& value)const{
    return snapshot() == value;
}

bool SliderState::setValue(const f64 value){
    if(!IsFinite(value))
        return false;
    advanceRevision();
    m_inputGeneration = __hidden_ui_slider_state::NextIdentity();
    m_value = value;
    m_press = {};
    m_pressMoved = false;
    return true;
}

void SliderState::reset(){
    advanceRevision();
    advanceAdmission();
    m_inputGeneration = __hidden_ui_slider_state::NextIdentity();
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


void SliderState::advanceRevision(){
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}

void SliderState::advanceAdmission(){
    if(m_admissionGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_admissionGeneration;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

