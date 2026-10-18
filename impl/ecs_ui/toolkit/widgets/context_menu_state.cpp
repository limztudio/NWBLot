// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context_menu.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ContextMenuState::open(const Rect& anchor)noexcept{
    if(
        !IsFinite(anchor.x) || !IsFinite(anchor.y) || !IsFinite(anchor.width) || !IsFinite(anchor.height) || anchor.width < 0.0f || anchor.height < 0.0f
        || !VectorIsFinite(VectorAdd(VectorSet(anchor.x, anchor.y, 0.0f, 0.0f), VectorSet(anchor.width, anchor.height, 0.0f, 0.0f)), VectorComponentMask::s_XY)
    )
        return false;
    m_popup.close();
    m_popup.open();
    m_list.select(0u);
    if(!m_list.scrollTo(0.0))
        TerminateInvariant();
    m_anchor = anchor;
    advanceRevision();
    return true;
}

void ContextMenuState::close()noexcept{
    m_popup.close();
    m_list.select(0u);
    advanceRevision();
}

void ContextMenuState::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

