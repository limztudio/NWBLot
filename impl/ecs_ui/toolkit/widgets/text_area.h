// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_state.h"
#include <impl/ecs_ui/toolkit/widgets/scrollbar_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns copied scroll admission, separate from preferred-column navigation and the application model.
class TextAreaScrollState final{
    friend class Builder;
    friend class TextAreaState;


private:
    [[nodiscard]] ControlToken prepare(const WidgetState& widget, const PopupToken& popup, u64 stateGeneration, u64 stateRevision, const EditModel& model, b8 enabled, b8 readOnly, const ScrollViewportPlacement& placement, Point step)noexcept;
    void retire()noexcept;


private:
    void advanceRevision()noexcept{ AdvanceRevision(m_revision); }


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
    [[nodiscard]] b8 scrollTo(Point scroll)noexcept;
    void reset()noexcept;


private:
    void advanceRevision()noexcept{ AdvanceRevision(m_revision); }


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

