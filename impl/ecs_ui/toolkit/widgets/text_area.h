// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_state.h"
#include "text_area_scroll_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextAreaOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    LayoutSize height = { LayoutSizePolicy::Fixed, 160.0f };
    bool enabled = true;
    bool readOnly = false;
    f32 wheelLines = 3.0f;
};

// Model and viewport remain lent through the enclosing scope; public intents retire even identical prior loans.
class TextAreaState final : NoCopy{
    friend class Builder;


public:
    TextAreaState() = default;
    TextAreaState(TextAreaState&&) = delete;
    TextAreaState& operator=(TextAreaState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const{ return m_navigation.instanceGeneration(); }
    [[nodiscard]] u64 revision()const{ return m_revision; }
    [[nodiscard]] const EditBoxPlacement& placement()const{ return m_visual.placement; }
    [[nodiscard]] const ScrollViewportPlacement& scrollbars()const{ return m_scrollInput.m_placement; }
    [[nodiscard]] Point scroll()const{ return { m_visual.scroll, m_scrollY }; }
    [[nodiscard]] const EditNavigationState& navigation()const{ return m_navigation; }
    [[nodiscard]] bool focused()const{ return m_visual.focused; }
    // Explicit scrolling suppresses caret reveal until a later accepted text, selection or ownership intent.
    [[nodiscard]] bool scrollTo(Point scroll);
    void reset();


private:
    void advanceRevision();


private:
    EditBoxState m_visual;
    EditNavigationState m_navigation;
    TextAreaScrollState m_scrollInput;
    u64 m_revision = 1u;
    u64 m_compositionGeneration = 0u;
    f32 m_scrollY = 0.0f;
    bool m_revealCaret = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

