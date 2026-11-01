// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scrollbar.h"

#include <impl/ecs_ui/toolkit/edit/model.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>
#include <impl/ecs_ui/toolkit/state/store.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns copied scroll admission, separate from preferred-column navigation and the application model.
class TextAreaScrollState final{
    friend class Builder;
    friend class TextAreaState;


private:
    [[nodiscard]] ControlToken prepare(const WidgetState& widget, const PopupToken& popup, u64 stateGeneration, u64 stateRevision, const EditModel& model, b8 enabled, b8 readOnly, const ScrollViewportPlacement& placement, Point step)noexcept;
    [[nodiscard]] b8 updateOffsets(const Point scroll)noexcept{ return ScrollbarLayout::UpdateOffsets(scroll, m_placement); }
    void retire()noexcept;


private:
    void advanceRevision()noexcept;


private:
    TextAreaScrollSnapshot m_snapshot;
    ScrollViewportPlacement m_placement;
    Point m_step;
    u64 m_revision = 1u;
    b8 m_prepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

