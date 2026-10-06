// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_context_popup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool Geometry(const PopupScope& scope)noexcept{
    return
        IsFinite(scope.bounds.x) && IsFinite(scope.bounds.y) && IsFinite(scope.bounds.width) && IsFinite(scope.bounds.height)
        && IsFinite(scope.viewport.x) && IsFinite(scope.viewport.y) && IsFinite(scope.viewport.width) && IsFinite(scope.viewport.height)
        && scope.bounds.width > 0.0f && scope.bounds.height > 0.0f && scope.viewport.width > 0.0f && scope.viewport.height > 0.0f
        && IsFinite(scope.bounds.x + scope.bounds.width) && IsFinite(scope.bounds.y + scope.bounds.height)
        && IsFinite(scope.viewport.x + scope.viewport.width) && IsFinite(scope.viewport.y + scope.viewport.height)
        && scope.bounds.x >= scope.viewport.x && scope.bounds.y >= scope.viewport.y
        && scope.bounds.x + scope.bounds.width <= scope.viewport.x + scope.viewport.width
        && scope.bounds.y + scope.bounds.height <= scope.viewport.y + scope.viewport.height
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Context::registerPopupScope(const WidgetState& state, PopupScope scope){
    if(
        m_failed || !currentDeclaration(state) || state.kind != WidgetKind::Popup
        || !scope.token.valid() || scope.token.widget != state.id || scope.token.declarationGeneration != state.declarationGeneration
        || scope.parent != m_currentPopup || m_popups.size() == s_InputMaxPopups || !__hidden_ui_context_popup::Geometry(scope)
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

bool Context::activatePopupScope(const PopupToken& token){
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

bool Context::updatePopupScope(const PopupToken& token, PopupScope scope){
    if(m_failed || !m_rootActive || !token.valid() || scope.token != token || !__hidden_ui_context_popup::Geometry(scope)){
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

bool Context::beginPopupScope(const WidgetState& state, PopupScope scope){
    return registerPopupScope(state, scope) && activatePopupScope(scope.token);
}

bool Context::endPopupScope(const bool visible){
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

void Context::discardPopupScope(const PopupToken& token){
    Array<bool, s_InputMaxPopups> removed{};
    bool found = false;
    for(usize index = 0u; index < m_popups.size(); ++index){
        removed[index] = m_popups[index].scope.token == token;
        for(usize ancestor = 0u; ancestor < index && !removed[index]; ++ancestor)
            removed[index] = removed[ancestor] && m_popups[index].scope.parent == m_popups[ancestor].scope.token;
        found |= removed[index];
        if(!removed[index])
            continue;
        for(usize active = 0u; active < m_popupDepth; ++active){
            if(m_popupStack[active] == m_popups[index].scope.token){
                fail();
                return;
            }
        }
    }
    if(!found)
        return;
    m_input.closePopup(token);
    for(usize index = m_targets.size(); index > 0u; --index){
        bool remove = false;
        for(usize popup = 0u; popup < m_popups.size(); ++popup)
            remove |= removed[popup] && m_targets[index - 1u].target.popup == m_popups[popup].scope.token;
        if(remove)
            m_targets.erase(m_targets.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_popups.size(); index > 0u; --index){
        if(removed[index - 1u])
            m_popups.erase(m_popups.begin() + static_cast<isize>(index - 1u));
    }
}

bool Context::hasPopupScope(const PopupToken& token)const noexcept{
    return popupLayer(token) != 0u;
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

