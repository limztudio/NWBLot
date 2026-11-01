// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_scroll_state.h"

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
        model.instanceGeneration(),
        model.revision(),
        model.externalRevision(),
        model.selectionGeneration(),
        model.compositionGeneration(),
        model.anchor(),
        model.caret(),
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
    return { stateGeneration, next.modelGeneration, m_revision };
}

void TextAreaScrollState::retire()noexcept{
    advanceRevision();
    m_prepared = false;
    m_snapshot = {};
    m_placement = {};
    m_step = {};
}

void TextAreaScrollState::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

