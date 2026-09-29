// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "popup.h"
#include "../input/popup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TooltipOptions{
    bool enabled = true;
    f32 delaySeconds = 0.5f;
    f32 maximumWidth = 320.0f;
    f32 gap = 4.0f;
    PopupPlacementSide::Enum side = PopupPlacementSide::Below;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host owns each state and lends it through the matching endPanel/endWindow/endPopup.
class TooltipState final : NoCopy{
    friend class Builder;
    friend class TooltipBehavior;


public:
    TooltipState();


public:
    TooltipState(TooltipState&&) = delete;
    TooltipState& operator=(TooltipState&&) = delete;


public:
    void reset();
    [[nodiscard]] u64 instanceGeneration()const{ return m_instanceGeneration; }
    [[nodiscard]] u64 revision()const{ return m_revision; }
    [[nodiscard]] bool visible()const{ return m_visible; }
    [[nodiscard]] const PopupPlacement& placement()const{ return m_placement; }


private:
    void advanceRevision();


private:
    const u64 m_instanceGeneration;
    u64 m_revision = 1u;
    WidgetId m_owner;
    u64 m_ownerDeclaration = 0u;
    WidgetId m_anchor;
    u64 m_declarationGeneration = 0u;
    PopupToken m_popup;
    u64 m_focusLossGeneration = 0u;
    TooltipOptions m_options;
    PopupPlacement m_placement;
    f64 m_elapsed = 0.0;
    bool m_hovered = false;
    bool m_visible = false;
};

class TooltipBehavior final{
public:
    // A new hover starts at zero; only contiguous accepted hover updates accrue time. Invalid inputs preserve the state.
    [[nodiscard]] static bool Update(TooltipState& state, WidgetId anchor, u64 declarationGeneration,
        const PopupToken& popup, u64 focusLossGeneration, bool hovered, f32 deltaSeconds, const TooltipOptions& options);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

