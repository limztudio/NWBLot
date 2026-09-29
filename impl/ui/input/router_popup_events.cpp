// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::consumePopupDismissal(const PopupToken& token, PopupDismissReason::Enum& reason){
    for(usize index = 0u; index < m_popupDismissals.size(); ++index){
        if(m_popupDismissals[index].token == token){
            reason = m_popupDismissals[index].reason;
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index));
            return true;
        }
    }
    return false;
}

bool InputRouter::dismissPopup(const PopupDismissReason::Enum reason){
    if(m_popups.empty() || reason <= PopupDismissReason::None || reason > PopupDismissReason::FocusLost)
        return false;
    auto& top = m_popups.back();
    if(
        top.closing || (reason == PopupDismissReason::Escape && !top.scope.dismissEscape)
        || (reason == PopupDismissReason::OutsideClick && !top.scope.dismissOutside)
    )
        return false;
    if(m_popupDismissals.size() == s_InputMaxPopups)
        return false;
    m_popupDismissals.push_back({ top.scope.token, reason });
    closePopup(top.scope.token);
    return true;
}

void InputRouter::closePopup(const PopupToken& token){
    for(auto& record : m_popups){
        if(record.scope.token == token)
            record.closing = true;
    }
    reconcileTargets();
}

void InputRouter::fencePopup(const PopupToken& token){
    for(auto& record : m_popups){
        if(record.scope.token.widget == token.widget && record.scope.token != token)
            record.closing = true;
    }
    reconcileTargets();
}

void InputRouter::cancelPopupFocus(){
    for(auto& record : m_popups){
        // Native focus loss must not restore keyboard focus into an unfocused window.
        record.restoreFocus = {};
        record.restoreDeclaration = 0u;
        record.restorePopup = {};
        if(record.closing)
            continue;
        if(m_popupDismissals.size() < s_InputMaxPopups)
            m_popupDismissals.push_back({ record.scope.token, PopupDismissReason::FocusLost });
        record.closing = true;
    }
}

void InputRouter::retirePopup(const WidgetId id){
    bool removed = false;
    const bool top = !m_popups.empty() && m_popups.back().scope.token.widget == id;
    for(usize index = m_popups.size(); index > 0u; --index){
        if(m_popups[index - 1u].scope.token.widget == id){
            const auto restore = m_popups[index - 1u];
            m_popups.erase(m_popups.begin() + static_cast<isize>(index - 1u));
            for(auto& record : m_popups){
                if(record.restorePopup == restore.scope.token){
                    record.restoreFocus = restore.restoreFocus;
                    record.restoreDeclaration = restore.restoreDeclaration;
                    record.restorePopup = restore.restorePopup;
                }
            }
            if(top){
                m_focus = restore.restoreFocus;
                m_focusDeclaration = restore.restoreDeclaration;
            }
            removed = true;
        }
    }
    for(usize index = m_popupDismissals.size(); index > 0u; --index){
        if(m_popupDismissals[index - 1u].token.widget == id)
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index - 1u));
    }
    if(removed)
        reconcileTargets();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

