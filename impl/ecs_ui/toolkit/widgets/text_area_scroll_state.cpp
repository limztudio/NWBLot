// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_scroll_state.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_area_scroll_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SameRect(const Rect& first, const Rect& second)noexcept{
    return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
}

[[nodiscard]] static bool SameBar(const ScrollbarPlacement& first, const ScrollbarPlacement& second)noexcept{
    // Offset and thumb position change during an ordinary drag without changing its accepted press geometry.
    return
        SameRect(first.track, second.track) && first.thumb.width == second.thumb.width && first.thumb.height == second.thumb.height
        && first.contentExtent == second.contentExtent && first.viewportExtent == second.viewportExtent
        && first.maximum == second.maximum && first.visible == second.visible
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ControlToken TextAreaScrollState::prepare(const WidgetState& widget, const PopupToken& popup,
    const u64 stateGeneration, const u64 stateRevision, const EditModel& model, const bool enabled, const bool readOnly,
    const ScrollViewportPlacement& placement,
    const Point step
)noexcept{
    const Snapshot next{ widget.id, popup, widget.declarationGeneration, stateRevision, model.instanceGeneration(),
        model.revision(), model.externalRevision(), model.selectionGeneration(), model.compositionGeneration(),
        model.anchor(), model.caret(), enabled, readOnly };
    if(
        !m_prepared || next.owner != m_snapshot.owner || next.popup != m_snapshot.popup
        || next.declarationGeneration != m_snapshot.declarationGeneration || next.stateRevision != m_snapshot.stateRevision
        || next.modelGeneration != m_snapshot.modelGeneration || next.modelRevision != m_snapshot.modelRevision
        || next.externalRevision != m_snapshot.externalRevision || next.selectionGeneration != m_snapshot.selectionGeneration
        || next.compositionGeneration != m_snapshot.compositionGeneration || next.anchor != m_snapshot.anchor || next.caret != m_snapshot.caret
        || next.enabled != m_snapshot.enabled || next.readOnly != m_snapshot.readOnly
        || !__hidden_text_area_scroll_state::SameRect(placement.viewport, m_placement.viewport)
        || !__hidden_text_area_scroll_state::SameRect(placement.contentClip, m_placement.contentClip)
        || !__hidden_text_area_scroll_state::SameRect(placement.corner, m_placement.corner)
        || !__hidden_text_area_scroll_state::SameBar(placement.horizontal, m_placement.horizontal)
        || !__hidden_text_area_scroll_state::SameBar(placement.vertical, m_placement.vertical)
        || step.x != m_step.x || step.y != m_step.y
    )
        advanceRevision();
    m_snapshot = next;
    m_placement = placement;
    m_step = step;
    m_prepared = true;
    return { stateGeneration, next.modelGeneration, m_revision };
}

bool TextAreaScrollState::updateOffsets(const Point scroll)noexcept{
    return ScrollbarLayout::UpdateOffsets(scroll, m_placement);
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

