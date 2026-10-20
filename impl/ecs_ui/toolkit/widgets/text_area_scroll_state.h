// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scrollbar.h"

#include <impl/ecs_ui/toolkit/edit/model.h>
#include <impl/ecs_ui/toolkit/input/input.h>
#include <impl/ecs_ui/toolkit/state/store.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Activation values of the track parts before and after each scrollbar thumb.
namespace TextAreaScrollTarget{
    inline constexpr u32 s_HorizontalBefore = 1u;
    inline constexpr u32 s_HorizontalAfter = 2u;
    inline constexpr u32 s_VerticalBefore = 3u;
    inline constexpr u32 s_VerticalAfter = 4u;
};

// Owns copied scroll admission, separate from preferred-column navigation and the application model.
class TextAreaScrollState final{
    friend class Builder;
    friend class TextAreaState;


private:
    struct Snapshot{
        WidgetId owner;
        PopupToken popup;
        u64 declarationGeneration = 0u;
        u64 stateRevision = 0u;
        u64 modelGeneration = 0u;
        u64 modelRevision = 0u;
        u64 externalRevision = 0u;
        u64 selectionGeneration = 0u;
        u64 compositionGeneration = 0u;
        usize anchor = 0u;
        usize caret = 0u;
        bool enabled = false;
        bool readOnly = false;
    };


private:
    [[nodiscard]] ControlToken prepare(const WidgetState& widget, const PopupToken& popup, u64 stateGeneration, u64 stateRevision, const EditModel& model, bool enabled, bool readOnly, const ScrollViewportPlacement& placement, Point step)noexcept;
    [[nodiscard]] bool updateOffsets(Point scroll)noexcept;
    void retire()noexcept;


private:
    void advanceRevision()noexcept;


private:

    Snapshot m_snapshot;
    ScrollViewportPlacement m_placement;
    Point m_step;
    u64 m_revision = 1u;
    bool m_prepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

