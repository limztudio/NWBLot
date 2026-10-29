// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "popup.h"
#include <impl/ecs_ui/toolkit/widgets/tooltip_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host owns each state and lends it through the matching endPanel/endWindow/endPopup.
class TooltipState final : NoCopy{
    friend class Builder;
    friend class TooltipBehavior;


public:
    TooltipState()noexcept;
    TooltipState(TooltipState&&) = delete;


public:
    TooltipState& operator=(TooltipState&&) = delete;


public:
    void reset()noexcept;
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] b8 visible()const noexcept{ return m_visible; }

    [[nodiscard]] const PopupPlacement& placement()const noexcept{ return m_placement; }


private:
    void advanceRevision()noexcept;


private:
    const u64 m_instanceGeneration;
    u64 m_revision = 1u;
    WidgetId m_owner;
    u64 m_ownerDeclaration = 0u;
    WidgetId m_anchor;
    u64 m_declarationGeneration = 0u;
    PopupToken m_popup;
    u64 m_focusLossGeneration = 0u;
    u64 m_hoverActivityGeneration = 0u;
    TooltipOptions m_options;
    PopupPlacement m_placement;
    b8 m_hovered = false;
    b8 m_visible = false;
    f64 m_elapsed = 0.0;
};

class TooltipBehavior final{
public:
    // A new hover starts at zero; only contiguous accepted hover updates accrue time. Invalid inputs preserve the state.
    [[nodiscard]] static b8 Update(TooltipState& state, WidgetId anchor, u64 declarationGeneration, const PopupToken& popup, u64 focusLossGeneration, u64 hoverActivityGeneration, b8 hovered, f32 deltaSeconds, const TooltipOptions& options)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

