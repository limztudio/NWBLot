// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Context::registerPopupScope(const WidgetState& state, PopupScope scope){
    if(
        m_failed || !currentDeclaration(state) || state.kind != WidgetKind::Popup
        || !scope.token.valid() || scope.token.widget != state.id || scope.token.declarationGeneration != state.declarationGeneration
        || scope.parent != m_currentPopup || m_popups.size() == s_InputMaxPopups || !HasValidPopupScopeGeometry(scope)
    ){
        fail();
        return false;
    }
    for(const auto& previous : m_popups){
        if(previous.scope.token.widget == scope.token.widget){
            fail();
            return false;
        }
    }
    scope.layer = m_popups.empty() ? 1u : m_popups.back().scope.layer + 1u;
    m_input.fencePopup(scope.token);
    m_popups.push_back({ scope, state.root });
    return true;
}

b8 Context::activatePopupScope(const PopupToken& token)noexcept{
    if(m_failed || !m_rootActive || !token.valid() || m_popupDepth == s_InputMaxPopups){
        fail();
        return false;
    }
    for(const auto& owned : m_popups){
        if(owned.scope.token != token)
            continue;
        if(!(owned.root == m_root) || owned.scope.parent != m_currentPopup){
            fail();
            return false;
        }
        m_popupStack[m_popupDepth] = token;
        ++m_popupDepth;
        m_currentPopup = token;
        m_popupLayer = owned.scope.layer;
        return true;
    }
    fail();
    return false;
}

b8 Context::updatePopupScope(const PopupToken& token, PopupScope scope)noexcept{
    if(m_failed || !m_rootActive || !token.valid() || scope.token != token || !HasValidPopupScopeGeometry(scope)){
        fail();
        return false;
    }
    for(auto& owned : m_popups){
        if(owned.scope.token != token)
            continue;
        if(!(owned.root == m_root) || scope.parent != owned.scope.parent || (scope.layer != 0u && scope.layer != owned.scope.layer)){
            fail();
            return false;
        }
        scope.layer = owned.scope.layer;
        owned.scope = scope;
        return true;
    }
    fail();
    return false;
}

b8 Context::endPopupScope(const b8 visible)noexcept{
    if(m_popupDepth == 0u || !m_currentPopup.valid()){
        fail();
        return false;
    }
    const PopupToken ended = m_currentPopup;
    --m_popupDepth;
    m_popupStack[m_popupDepth] = {};
    m_currentPopup = m_popupDepth == 0u ? PopupToken{} : m_popupStack[m_popupDepth - 1u];
    m_popupLayer = popupLayer(m_currentPopup);
    if(!visible)
        discardPopupScope(ended);
    return !m_failed;
}

void Context::discardPopupScope(const PopupToken& token)noexcept{
    Array<b8, s_InputMaxPopups> removed{};
    if(!MarkRemovedPopupSubtree(m_popups, [&](const PopupToken& candidate)noexcept{ return candidate == token; }, removed))
        return;
    for(usize active = 0u; active < m_popupDepth; ++active){
        if(IsRemovedPopupToken(m_popups, removed, m_popupStack[active])){
            fail();
            return;
        }
    }
    m_input.closePopup(token);
    EraseIfRemovedPopupToken(m_targets, m_popups, removed, [](const ContextOwnedTarget& owned)noexcept -> const PopupToken&{ return owned.target.popup; });
    EraseRemovedPopups(m_popups, removed);
}

u32 Context::popupLayer(const PopupToken& token)const noexcept{
    for(const auto& owned : m_popups){
        if(owned.scope.token == token)
            return owned.scope.layer;
    }
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

