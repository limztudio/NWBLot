// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PopupDismissReason::Enum> InputRouter::consumePopupDismissal(const PopupToken& token)noexcept{
    for(usize index = 0u; index < m_popupDismissals.size(); ++index){
        if(m_popupDismissals[index].token == token){
            const PopupDismissReason::Enum reason = m_popupDismissals[index].reason;
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index));
            return reason;
        }
    }
    return MakeUnexpected(Failure{});
}

b8 InputRouter::dismissPopup(const PopupDismissReason::Enum reason){
    if(m_popups.empty() || reason <= PopupDismissReason::None || reason > PopupDismissReason::FocusLost)
        return false;
    auto& top = m_popups.back();
    if(top.closing || (reason == PopupDismissReason::Cancel && !top.scope.dismissCancel) || (reason == PopupDismissReason::OutsideClick && !top.scope.dismissOutside) || (reason == PopupDismissReason::FocusTraversal && !top.scope.dismissFocusTraversal))
        return false;
    if(m_popupDismissals.size() == s_InputMaxPopups)
        return false;
    m_popupDismissals.push_back({ top.scope.token, reason });
    closePopup(top.scope.token);
    return true;
}

void InputRouter::closePopup(const PopupToken& token)noexcept{
    for(auto& record : m_popups){
        if(popupDescendant(record.scope.token, token))
            record.closing = true;
    }
    reconcileTargets();
}

void InputRouter::fencePopup(const PopupToken& token)noexcept{
    for(auto& record : m_popups){
        if(record.scope.token.widget == token.widget && record.scope.token != token)
            closePopup(record.scope.token);
    }
    reconcileTargets();
}

const PopupScope* InputRouter::popupScope(const PopupToken& token)const noexcept{
    for(const auto& record : m_popups){
        if(record.scope.token == token)
            return &record.scope;
    }
    return nullptr;
}

b8 InputRouter::allowedByPopup(const HitTarget& target)const noexcept{
    if(m_popups.empty())
        return !target.popup.widget.valid();
    const PopupRecord& top = m_popups.back();
    return !top.closing && target.popup == top.scope.token;
}

b8 InputRouter::popupDescendant(const PopupToken& token, const PopupToken& ancestor)const noexcept{
    PopupToken current = token;
    for(usize depth = 0u; depth < s_InputMaxPopups && current.valid(); ++depth){
        if(current == ancestor)
            return true;
        b8 found = false;
        for(const auto& record : m_popups){
            if(record.scope.token == current){
                current = record.scope.parent;
                found = true;
                break;
            }
        }
        if(!found)
            return false;
    }
    return false;
}

b8 InputRouter::stagePopups(const Span<const PopupScope> scopes){
    const usize count = scopes.size();
    if(count > s_InputMaxPopups)
        return false;
    m_stagedPopups.clear();
    for(usize index = 0u; index < count; ++index){
        const PopupScope& scope = scopes[index];
        if(
            !scope.token.valid() || !HasValidPopupScopeGeometry(scope)
            || scope.layer == 0u || (index != 0u && scope.layer <= scopes[index - 1u].layer)
            || (scope.dismissFocusTraversal && (!scope.focusAnchor.valid() || scope.focusAnchorDeclarationGeneration == 0u))
        )
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(scopes[previous].token.widget == scope.token.widget)
                return false;
        }
        if(!scope.parent.empty()){
            b8 parent = false;
            for(usize previous = 0u; previous < index; ++previous)
                parent |= scopes[previous].token == scope.parent;
            if(!scope.parent.valid() || !parent)
                return false;
        }
        // A child cannot reuse its open lifetime beneath a different ancestor lifetime.
        for(const auto& previous : m_popups){
            if(previous.scope.token == scope.token && previous.scope.parent != scope.parent)
                return false;
        }
        PopupRecord record;
        record.scope = scope;
        m_stagedPopups.push_back(record);
    }
    return true;
}

b8 InputRouter::validPopupTarget(const HitTarget& target)const noexcept{
    if(!target.popup.widget.valid())
        return target.layer == 0u && target.popup.declarationGeneration == 0u && target.popup.instanceGeneration == 0u && target.popup.openGeneration == 0u;
    for(const auto& record : m_stagedPopups){
        if(record.scope.token == target.popup)
            return target.layer == record.scope.layer;
    }
    return false;
}

void InputRouter::installPopups(const u64 expectedFocusLossGeneration){
    for(usize index = m_popupDismissals.size(); index > 0u; --index){
        b8 retained = false;
        for(const auto& record : m_stagedPopups)
            retained |= record.scope.token == m_popupDismissals[index - 1u].token;
        if(!retained)
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index - 1u));
    }
    const PopupToken previousTop = m_popups.empty() ? PopupToken{} : m_popups.back().scope.token;
    const PopupToken nextTop = m_stagedPopups.empty() ? PopupToken{} : m_stagedPopups.back().scope.token;
    const auto eligible = [this](const WidgetId id, const u64 declaration, const PopupToken& scope)noexcept{
        const HitTarget* target = findTarget(id, declaration);
        return
            target && target->enabled && target->focusable
            && HasVisibleBounds(*target)
            && target->popup == scope
        ;
    };
    // Rebuild the return chain for the accepted order, including insertions beneath a retained top popup.
    WidgetId candidateFocus = m_popups.empty() ? m_focus : m_popups.front().restoreFocus;
    u64 candidateDeclaration = m_popups.empty() ? m_focusDeclaration : m_popups.front().restoreDeclaration;
    PopupToken candidatePopup;
    const b8 lostFocus = !m_windowFocused || (expectedFocusLossGeneration != Limit<u64>::s_Max && expectedFocusLossGeneration != m_focusLossGeneration);
    if(!eligible(candidateFocus, candidateDeclaration, candidatePopup)){
        candidateFocus = {};
        candidateDeclaration = 0u;
    }
    for(auto& staged : m_stagedPopups){
        for(const auto& previous : m_popups){
            if(previous.scope.token == staged.scope.token){
                staged.closing = previous.closing;
                break;
            }
        }
        for(const auto& ancestor : m_stagedPopups){
            if(ancestor.scope.token == staged.scope.parent && ancestor.closing)
                staged.closing = true;
        }
        if(lostFocus){
            if(!staged.closing && m_popupDismissals.size() < s_InputMaxPopups)
                m_popupDismissals.push_back({ staged.scope.token, PopupDismissReason::FocusLost });
            staged.closing = true;
            candidateFocus = {};
            candidateDeclaration = 0u;
        }
        staged.restoreFocus = candidateFocus;
        staged.restoreDeclaration = candidateDeclaration;
        staged.restorePopup = lostFocus ? PopupToken{} : candidatePopup;
        candidateFocus = {};
        candidateDeclaration = 0u;
        candidatePopup = staged.scope.token;
        if(staged.closing)
            continue;
        if(candidatePopup == previousTop){
            if(eligible(m_focus, m_focusDeclaration, candidatePopup)){
                candidateFocus = m_focus;
                candidateDeclaration = m_focusDeclaration;
            }
            continue;
        }
        // A higher popup records the last focused child of the scope it covered.
        for(const auto& previous : m_popups){
            if(previous.restorePopup == candidatePopup && eligible(previous.restoreFocus, previous.restoreDeclaration, candidatePopup)){
                candidateFocus = previous.restoreFocus;
                candidateDeclaration = previous.restoreDeclaration;
                break;
            }
        }
        if(!candidateFocus.valid() && staged.scope.autofocus){
            for(const auto& target : m_targets){
                if(
                    target.popup == candidatePopup && target.enabled && target.focusable
                    && HasVisibleBounds(target)
                ){
                    candidateFocus = target.id;
                    candidateDeclaration = target.declarationGeneration;
                    break;
                }
            }
        }
    }
    m_popups.swap(m_stagedPopups);
    if(previousTop != nextTop){
        retireCommandFocus();
        m_focus = candidateFocus;
        m_focusDeclaration = candidateDeclaration;
        const HitTarget* target = findTarget(m_focus, m_focusDeclaration);
        m_focusControl = target ? target->control : ControlToken{};
    }
}

void InputRouter::retirePopup(const WidgetId id){
    Array<b8, s_InputMaxPopups> removed{};
    if(!MarkRemovedPopupSubtree(m_popups, [&](const PopupToken& candidate)noexcept{ return candidate.widget == id; }, removed))
        return;
    const b8 top = removed[m_popups.size() - 1u];
    PopupRecord restore = m_popups.back();
    const auto splice = [this, &removed](PopupRecord& record)noexcept{
        for(usize depth = 0u; depth < s_InputMaxPopups; ++depth){
            b8 foundParent = false;
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
    EraseIfRemovedPopupToken(m_targets, m_popups, removed, [](const HitTarget& target)noexcept -> const PopupToken&{ return target.popup; });
    EraseIfRemovedPopupToken(m_popupDismissals, m_popups, removed, [](const PopupDismissal& dismissal)noexcept -> const PopupToken&{ return dismissal.token; });
    EraseRemovedPopups(m_popups, removed);
    rebuildLookup();
    if(top){
        retireCommandFocus();
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

void InputRouter::cancelPopupFocus(){
    for(auto& record : m_popups){
        // Window focus loss must not restore command focus into an unfocused window.
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

