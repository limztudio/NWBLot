// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scroll.h"

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
    return
        IsFinite(contentHeight) && contentHeight >= 0.0
        && IsFinite(viewportHeight) && viewportHeight >= 0.0
    ;
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
    if(
        !IsFinite(start) || start < 0.0 || !IsFinite(end) || end < start
        || !IsFinite(viewportHeight) || viewportHeight < 0.0
    )
        return false;
    f64 candidate = m_offset;
    if(end - start > viewportHeight || start < candidate)
        candidate = start;
    else if(end > candidate && end - candidate > viewportHeight)
        candidate = end - viewportHeight;
    m_offset = candidate;
    return true;
}

bool ScrollState::scrollBy(const f64 delta, const f64 contentHeight, const f64 viewportHeight)noexcept{
    if(!IsFinite(delta) || !__hidden_ui_scroll_state::ValidRange(contentHeight, viewportHeight))
        return false;
    const f64 maxOffset = Max(0.0, contentHeight - viewportHeight);
    const f64 base = Min(m_offset, maxOffset);
    if(delta >= 0.0)
        m_offset = base + Min(delta, maxOffset - base);
    else
        m_offset = Max(0.0, base + delta);
    return true;
}

bool ScrollState::clamp(const f64 contentHeight, const f64 viewportHeight)noexcept{
    if(!__hidden_ui_scroll_state::ValidRange(contentHeight, viewportHeight))
        return false;
    m_offset = Min(m_offset, Max(0.0, contentHeight - viewportHeight));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

