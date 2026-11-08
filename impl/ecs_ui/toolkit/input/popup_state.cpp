// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputPopupState::InputPopupState(Core::Alloc::GlobalArena& arena)
    : m_popups(arena)
    , m_stagedPopups(arena)
    , m_popupDismissals(arena)
{
    m_popups.reserve(s_InputMaxPopups);
    m_stagedPopups.reserve(s_InputMaxPopups);
    m_popupDismissals.reserve(s_InputMaxPopups);
}


b8 InputPopupState::stage(const Span<const PopupScope> scopes)noexcept{
    return TryStageRouterPopups(scopes, m_popups, m_stagedPopups);
}


Expected<PopupDismissReason::Enum> InputPopupState::consumeDismissal(const PopupToken& token)noexcept{
    return TryConsumeRouterPopupDismissal(token, m_popupDismissals);
}


b8 InputPopupState::dismiss(const PopupDismissReason::Enum reason)noexcept{
    return TryDismissRouterPopup(reason, m_popups, m_popupDismissals);
}


void InputPopupState::close(const PopupToken& token)noexcept{
    CloseRouterPopupSubtree(token, m_popups);
}


void InputPopupState::fence(const PopupToken& token)noexcept{
    FenceRouterPopup(token, m_popups);
}


void InputPopupState::cancelFocus()noexcept{
    CancelRouterPopupFocus(m_popups, m_popupDismissals);
}


void InputPopupState::reset()noexcept{
    m_popups.clear();
    m_stagedPopups.clear();
    m_popupDismissals.clear();
}


b8 InputPopupState::apply(const PopupDismissRequest& request)noexcept{
    if(!request.requested || m_popups.empty() || m_popups.back().scope.token != request.expectedTop)
        return false;
    if(!TryDismissRouterPopup(request.reason, m_popups, m_popupDismissals))
        return false;
    if(request.replaceRestore){
        PopupRecord& closing = m_popups.back();
        closing.restoreFocus = request.restoreFocus;
        closing.restoreDeclaration = request.restoreDeclaration;
        closing.restorePopup = request.restorePopup;
    }
    return true;
}


Optional<InputPopupRetirement> InputPopupState::retire(InputTargetTable& targets, const WidgetId id){
    return targets.retirePopup(id, m_popups, m_popupDismissals);
}


void InputPopupState::install(
        const Span<const HitTarget> targets,
        const Span<const TargetLookup> lookup,
        const b8 windowFocused,
        const u64 focusLossGeneration,
        const u64 expectedFocusLossGeneration,
        Vector<CommandSource>& commandSources,
        WidgetId& inOutFocus,
        u64& inOutFocusDeclaration,
        ControlToken& inOutFocusControl
    )noexcept{
    InstallRouterPopups(
        targets,
        lookup,
        windowFocused,
        focusLossGeneration,
        expectedFocusLossGeneration,
        m_popupDismissals,
        m_stagedPopups,
        m_popups,
        commandSources,
        inOutFocus,
        inOutFocusDeclaration,
        inOutFocusControl
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

