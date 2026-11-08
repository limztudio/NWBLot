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
    friend class TooltipCoordinator;


private:
    [[nodiscard]] static u64 NextIdentity()noexcept{
        static Atomic<u64> s_NextIdentity{ 1u };
        return NextNonWrappingIdentity(s_NextIdentity);
    }


public:
    TooltipState()noexcept
        : m_instanceGeneration(NextIdentity())
    {}
    TooltipState(TooltipState&&) = delete;


public:
    TooltipState& operator=(TooltipState&&) = delete;


public:
    void reset()noexcept{
        AdvanceNonWrappingRevision(m_revision);
        ResetTooltipStateFields(
            m_owner,
            m_ownerDeclaration,
            m_anchor,
            m_declarationGeneration,
            m_popup,
            m_focusLossGeneration,
            m_hoverActivityGeneration,
            m_options,
            m_placement,
            m_elapsed,
            m_hovered,
            m_visible
        );
    }
    [[nodiscard]] u64 instanceGeneration()const noexcept{ return m_instanceGeneration; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] b8 visible()const noexcept{ return m_visible; }

    [[nodiscard]] const PopupPlacement& placement()const noexcept{ return m_placement; }


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
    [[nodiscard]] static b8 Update(
        TooltipState& state, const WidgetId anchor, const u64 declarationGeneration,
        const PopupToken& popup, const u64 focusLossGeneration, const u64 hoverActivityGeneration,
        const b8 hovered, const f32 deltaSeconds, const TooltipOptions& options
    )noexcept{
        const auto changed = TryEvaluateTooltipStateUpdate(
            anchor,
            declarationGeneration,
            popup,
            focusLossGeneration,
            hoverActivityGeneration,
            hovered,
            deltaSeconds,
            options,
            state.m_anchor,
            state.m_declarationGeneration,
            state.m_popup,
            state.m_focusLossGeneration,
            state.m_hoverActivityGeneration,
            state.m_options,
            state.m_placement,
            state.m_hovered,
            state.m_visible,
            state.m_elapsed
        );
        if(!changed)
            return false;
        if(*changed)
            AdvanceNonWrappingRevision(state.m_revision);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

