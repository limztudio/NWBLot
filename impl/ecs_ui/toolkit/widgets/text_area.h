// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_state.h"
#include <impl/ecs_ui/toolkit/widgets/text_area_scroll.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns copied scroll admission, separate from preferred-column navigation and the application model.
class TextAreaScrollState final{
    friend class Builder;
    friend class TextAreaState;


private:
    [[nodiscard]] ControlToken prepare(
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
            || !SameScrollViewportPlacementGeometry(placement, m_placement)
            || step.x != m_step.x || step.y != m_step.y
        )
            AdvanceNonWrappingRevision(m_revision);
        m_snapshot = next;
        m_placement = placement;
        m_step = step;
        m_prepared = true;
        return { stateGeneration, next.model.instanceGeneration, m_revision };
    }
    void retire()noexcept{
        AdvanceNonWrappingRevision(m_revision);
        m_prepared = false;
        m_snapshot = {};
        m_placement = {};
        m_step = {};
    }


private:
    TextAreaScrollSnapshot m_snapshot;
    ScrollViewportPlacement m_placement;
    Point m_step;
    u64 m_revision = 1u;
    b8 m_prepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Model and viewport remain lent through the enclosing scope; public intents retire even identical prior loans.
class TextAreaState final : NoCopy{
    friend class Builder;


public:
    TextAreaState()noexcept = default;
    TextAreaState(TextAreaState&&) = delete;


public:
    TextAreaState& operator=(TextAreaState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_navigation.instanceGeneration(); }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] const EditBoxPlacement& placement()const noexcept{ return m_visual.placement; }

    [[nodiscard]] const ScrollViewportPlacement& scrollbars()const noexcept{ return m_scrollInput.m_placement; }

    [[nodiscard]] Point scroll()const noexcept{ return { m_visual.scroll, m_scrollY }; }

    [[nodiscard]] const EditNavigationState& navigation()const noexcept{ return m_navigation; }

    [[nodiscard]] b8 focused()const noexcept{ return m_visual.focused; }
    // Explicit scrolling suppresses caret reveal until a later accepted text, selection or ownership intent.
    [[nodiscard]] b8 scrollTo(const Point scroll)noexcept{
        if(!IsValidUiExtent(scroll))
            return false;
        AdvanceNonWrappingRevision(m_revision);
        m_scrollInput.retire();
        m_visual.scroll = scroll.x;
        m_scrollY = scroll.y;
        m_revealCaret = false;
        return true;
    }
    void reset()noexcept{
        AdvanceNonWrappingRevision(m_revision);
        m_scrollInput.retire();
        m_visual = {};
        m_navigation.reset();
        m_compositionGeneration = 0u;
        m_scrollY = 0.0f;
        m_revealCaret = true;
    }


private:
    EditBoxState m_visual;
    EditNavigationState m_navigation;
    TextAreaScrollState m_scrollInput;
    u64 m_revision = 1u;
    u64 m_compositionGeneration = 0u;
    f32 m_scrollY = 0.0f;
    b8 m_revealCaret = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
