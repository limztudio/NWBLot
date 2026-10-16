// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scroll.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scroll_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRange(const f64 contentHeight, const f64 viewportHeight)noexcept{
    return IsFinite(contentHeight) && contentHeight >= 0.0 && IsFinite(viewportHeight) && viewportHeight >= 0.0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ScrollState::ScrollState()noexcept
    : m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_scroll_state::s_NextIdentity))
{}

bool ScrollState::setOffset(const f64 offset)noexcept{
    if(!IsFinite(offset) || offset < 0.0)
        return false;
    m_offset = offset;
    return true;
}

bool ScrollState::ensureVisible(const f64 start, const f64 end, const f64 viewportHeight)noexcept{
    if(!IsFinite(start) || start < 0.0 || !IsFinite(end) || end < start || !IsFinite(viewportHeight) || viewportHeight < 0.0)
        return false;
    f64 candidate = m_offset;
    if(VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(end), VectorDoubleReplicate(start))) > viewportHeight || start < candidate)
        candidate = start;
    else if(end > candidate && VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(end), VectorDoubleReplicate(candidate))) > viewportHeight)
        candidate = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(end), VectorDoubleReplicate(viewportHeight)));
    m_offset = candidate;
    return true;
}

bool ScrollState::scrollBy(const f64 delta, const f64 contentHeight, const f64 viewportHeight)noexcept{
    if(!IsFinite(delta) || !__hidden_ui_scroll_state::ValidRange(contentHeight, viewportHeight))
        return false;
    const SIMDVectorDouble maxOffset = VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleReplicate(contentHeight), VectorDoubleReplicate(viewportHeight)));
    const SIMDVectorDouble base = VectorDoubleMin(VectorDoubleReplicate(m_offset), maxOffset);
    const SIMDVectorDouble deltaValue = VectorDoubleReplicate(delta);
    if(delta >= 0.0)
        m_offset = VectorDoubleGetX(VectorDoubleAdd(base, VectorDoubleMin(deltaValue, VectorDoubleSubtract(maxOffset, base))));
    else
        m_offset = VectorDoubleGetX(VectorDoubleMax(VectorDoubleZero(), VectorDoubleAdd(base, deltaValue)));
    return true;
}

bool ScrollState::clamp(const f64 contentHeight, const f64 viewportHeight)noexcept{
    if(!__hidden_ui_scroll_state::ValidRange(contentHeight, viewportHeight))
        return false;
    m_offset = VectorDoubleGetX(VectorDoubleMin(
        VectorDoubleReplicate(m_offset),
        VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleReplicate(contentHeight), VectorDoubleReplicate(viewportHeight)))
    ));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

