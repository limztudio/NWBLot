// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Context::beginPopupScope(const WidgetState& state, PopupScope scope){
    if(
        m_failed || !currentDeclaration(state) || state.kind != WidgetKind::Popup || m_currentPopup.valid()
        || !scope.token.valid() || scope.token.widget != state.id || scope.token.declarationGeneration != state.declarationGeneration
        || m_popups.size() == s_InputMaxPopups
        || !IsFinite(scope.bounds.x) || !IsFinite(scope.bounds.y) || !IsFinite(scope.bounds.width) || !IsFinite(scope.bounds.height)
        || !IsFinite(scope.viewport.x) || !IsFinite(scope.viewport.y) || !IsFinite(scope.viewport.width) || !IsFinite(scope.viewport.height)
        || scope.bounds.width <= 0.0f || scope.bounds.height <= 0.0f || scope.viewport.width <= 0.0f || scope.viewport.height <= 0.0f
        || !IsFinite(scope.bounds.x + scope.bounds.width) || !IsFinite(scope.bounds.y + scope.bounds.height)
        || !IsFinite(scope.viewport.x + scope.viewport.width) || !IsFinite(scope.viewport.y + scope.viewport.height)
        || scope.bounds.x < scope.viewport.x || scope.bounds.y < scope.viewport.y
        || scope.bounds.x + scope.bounds.width > scope.viewport.x + scope.viewport.width
        || scope.bounds.y + scope.bounds.height > scope.viewport.y + scope.viewport.height
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
    scope.layer = static_cast<u32>(m_popups.size() + 1u);
    m_input.fencePopup(scope.token);
    m_popups.push_back({ scope, state.root });
    m_currentPopup = scope.token;
    m_popupLayer = scope.layer;
    return true;
}

bool Context::endPopupScope(const bool visible){
    if(!m_currentPopup.valid()){
        fail();
        return false;
    }
    if(!visible){
        m_input.closePopup(m_currentPopup);
        for(usize index = m_targets.size(); index > 0u; --index){
            if(m_targets[index - 1u].target.popup == m_currentPopup)
                m_targets.erase(m_targets.begin() + static_cast<isize>(index - 1u));
        }
        m_popups.pop_back();
    }
    m_currentPopup = {};
    m_popupLayer = 0u;
    return !m_failed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

