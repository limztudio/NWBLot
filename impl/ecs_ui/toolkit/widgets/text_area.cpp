// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ControlToken TextAreaScrollState::prepare(
    const WidgetState& widget, const PopupToken& popup,
    const u64 stateGeneration, const u64 stateRevision, const EditModel& model, const b8 enabled, const b8 readOnly,
    const ScrollViewportPlacement& placement,
    const Point step
)noexcept{
    const TextAreaScrollSnapshot next(
        widget.id,
        popup,
        widget.declarationGeneration,
        stateRevision,
        model.stamp(),
        enabled,
        readOnly
    );
    if(
        !m_prepared || next != m_snapshot
        || !EqualUiRect(placement.viewport, m_placement.viewport)
        || !EqualUiRect(placement.contentClip, m_placement.contentClip)
        || !EqualUiRect(placement.corner, m_placement.corner)
        || !SameScrollbarPlacementGeometry(placement.horizontal, m_placement.horizontal)
        || !SameScrollbarPlacementGeometry(placement.vertical, m_placement.vertical)
        || step.x != m_step.x || step.y != m_step.y
    )
        advanceRevision();
    m_snapshot = next;
    m_placement = placement;
    m_step = step;
    m_prepared = true;
    return { stateGeneration, next.model.instanceGeneration, m_revision };
}

void TextAreaScrollState::retire()noexcept{
    advanceRevision();
    m_prepared = false;
    m_snapshot = {};
    m_placement = {};
    m_step = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextAreaState::scrollTo(const Point scroll)noexcept{
    if(!IsValidUiExtent(scroll))
        return false;
    advanceRevision();
    m_scrollInput.retire();
    m_visual.scroll = scroll.x;
    m_scrollY = scroll.y;
    m_revealCaret = false;
    return true;
}

void TextAreaState::reset()noexcept{
    advanceRevision();
    m_scrollInput.retire();
    m_visual = {};
    m_navigation.reset();
    m_compositionGeneration = 0u;
    m_scrollY = 0.0f;
    m_revealCaret = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

