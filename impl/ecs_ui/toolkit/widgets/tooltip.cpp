// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tooltip.h"

#include <global/simplemath.h>
#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_tooltip{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextIdentity{ 1u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TooltipState::TooltipState()noexcept
    : m_instanceGeneration(NextNonWrappingIdentity(__hidden_ui_tooltip::s_NextIdentity))
{}

void TooltipState::reset()noexcept{
    advanceRevision();
    m_owner = {};
    m_ownerDeclaration = 0u;
    m_anchor = {};
    m_declarationGeneration = 0u;
    m_popup = {};
    m_focusLossGeneration = 0u;
    m_hoverActivityGeneration = 0u;
    m_options = {};
    m_placement = {};
    m_elapsed = 0.0;
    m_hovered = false;
    m_visible = false;
}

void TooltipState::advanceRevision()noexcept{
    if(m_revision == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_revision;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TooltipBehavior::Update(
    TooltipState& state, const WidgetId anchor, const u64 declarationGeneration,
    const PopupToken& popup, const u64 focusLossGeneration, const u64 hoverActivityGeneration,
    const b8 hovered, const f32 deltaSeconds, const TooltipOptions& options
)noexcept{
    if(!anchor.valid() || declarationGeneration == 0u || (!popup.valid() && !(popup == PopupToken{})) || !IsFinite(deltaSeconds) || deltaSeconds < 0.0f || !IsValidTooltipOptions(options))
        return false;
    const b8 sameBinding = state.m_anchor == anchor && state.m_declarationGeneration == declarationGeneration && state.m_popup == popup && state.m_focusLossGeneration == focusLossGeneration && state.m_hoverActivityGeneration == hoverActivityGeneration;
    const b8 sameOptions = state.m_options == options;
    const b8 activeHover = options.enabled && hovered;
    f64 elapsed = 0.0;
    if(activeHover && state.m_hovered && sameBinding && sameOptions)
        elapsed = Min(state.m_elapsed + static_cast<f64>(deltaSeconds), static_cast<f64>(options.delaySeconds));
    const b8 visible = activeHover && elapsed >= static_cast<f64>(options.delaySeconds);
    if(!sameBinding || !sameOptions || state.m_hovered != activeHover || state.m_elapsed != elapsed || state.m_visible != visible){
        state.advanceRevision();
        state.m_anchor = anchor;
        state.m_declarationGeneration = declarationGeneration;
        state.m_popup = popup;
        state.m_focusLossGeneration = focusLossGeneration;
        state.m_hoverActivityGeneration = hoverActivityGeneration;
        state.m_options = options;
        state.m_elapsed = elapsed;
        state.m_hovered = activeHover;
        state.m_visible = visible;
        if(!visible || !sameBinding || !sameOptions)
            state.m_placement = {};
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

