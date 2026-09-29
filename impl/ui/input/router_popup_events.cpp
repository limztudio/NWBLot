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
        if(popupDescendant(record.scope.token, token))
            record.closing = true;
    }
    reconcileTargets();
}

void InputRouter::fencePopup(const PopupToken& token){
    for(auto& record : m_popups){
        if(record.scope.token.widget == token.widget && record.scope.token != token)
            closePopup(record.scope.token);
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
    Array<bool, s_InputMaxPopups> removed{};
    bool found = false;
    for(usize index = 0u; index < m_popups.size(); ++index){
        removed[index] = m_popups[index].scope.token.widget == id;
        for(usize ancestor = 0u; ancestor < index && !removed[index]; ++ancestor)
            removed[index] = removed[ancestor] && m_popups[index].scope.parent == m_popups[ancestor].scope.token;
        found |= removed[index];
    }
    if(!found)
        return;
    const bool top = removed[m_popups.size() - 1u];
    PopupRecord restore = m_popups.back();
    const auto splice = [this, &removed](PopupRecord& record){
        for(usize depth = 0u; depth < s_InputMaxPopups; ++depth){
            bool foundParent = false;
            for(usize index = 0u; index < m_popups.size(); ++index){
                if(removed[index] && record.restorePopup == m_popups[index].scope.token){
                    const auto& ancestor = m_popups[index];
                    record.restoreFocus = ancestor.restoreFocus;
                    record.restoreDeclaration = ancestor.restoreDeclaration;
                    record.restorePopup = ancestor.restorePopup;
                    foundParent = true;
                    break;
                }
            }
            if(!foundParent)
                break;
        }
    };
    if(top)
        splice(restore);
    for(usize index = 0u; index < m_popups.size(); ++index){
        if(!removed[index])
            splice(m_popups[index]);
    }
    for(usize index = m_targets.size(); index > 0u; --index){
        bool remove = false;
        for(usize popup = 0u; popup < m_popups.size(); ++popup)
            remove |= removed[popup] && m_targets[index - 1u].popup == m_popups[popup].scope.token;
        if(remove)
            m_targets.erase(m_targets.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_popupDismissals.size(); index > 0u; --index){
        bool remove = false;
        for(usize popup = 0u; popup < m_popups.size(); ++popup)
            remove |= removed[popup] && m_popupDismissals[index - 1u].token == m_popups[popup].scope.token;
        if(remove)
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_popups.size(); index > 0u; --index){
        if(removed[index - 1u])
            m_popups.erase(m_popups.begin() + static_cast<isize>(index - 1u));
    }
    rebuildLookup();
    if(top){
        const HitTarget* target = findTarget(restore.restoreFocus, restore.restoreDeclaration);
        if(m_windowFocused && target && target->popup == restore.restorePopup && target->focusable && isInteractive(*target)){
            m_focus = target->id;
            m_focusDeclaration = target->declarationGeneration;
            m_focusControl = target->control;
        }
        else
            clearFocus();
    }
    reconcileTargets();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

