// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_router_popup{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool Area(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width > 0.0f && rectangle.height > 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

[[nodiscard]] static bool Focusable(const HitTarget* target){
    return
        target && target->enabled && target->focusable
        && Min(target->rectangle.x + target->rectangle.width, target->clip.x + target->clip.width) > Max(target->rectangle.x, target->clip.x)
        && Min(target->rectangle.y + target->rectangle.height, target->clip.y + target->clip.height) > Max(target->rectangle.y, target->clip.y)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::stagePopups(const PopupScope* scopes, const usize count){
    if(count > s_InputMaxPopups || (count != 0u && !scopes))
        return false;
    m_stagedPopups.clear();
    for(usize index = 0u; index < count; ++index){
        const PopupScope& scope = scopes[index];
        if(
            !scope.token.valid() || !__hidden_ui_router_popup::Area(scope.bounds) || !__hidden_ui_router_popup::Area(scope.viewport)
            || scope.layer == 0u || (index != 0u && scope.layer <= scopes[index - 1u].layer)
            || (scope.dismissFocusTraversal && (!scope.focusAnchor.valid() || scope.focusAnchorDeclarationGeneration == 0u))
            || scope.bounds.x < scope.viewport.x || scope.bounds.y < scope.viewport.y
            || scope.bounds.x + scope.bounds.width > scope.viewport.x + scope.viewport.width
            || scope.bounds.y + scope.bounds.height > scope.viewport.y + scope.viewport.height
        )
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(scopes[previous].token.widget == scope.token.widget)
                return false;
        }
        if(!scope.parent.empty()){
            bool parent = false;
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

bool InputRouter::validPopupTarget(const HitTarget& target)const{
    if(!target.popup.widget.valid()){
        return
            target.layer == 0u && target.popup.declarationGeneration == 0u
            && target.popup.instanceGeneration == 0u && target.popup.openGeneration == 0u
        ;
    }
    for(const auto& record : m_stagedPopups){
        if(record.scope.token == target.popup)
            return target.layer == record.scope.layer;
    }
    return false;
}

const PopupScope* InputRouter::popupScope(const PopupToken& token)const{
    for(const auto& record : m_popups){
        if(record.scope.token == token)
            return &record.scope;
    }
    return nullptr;
}

bool InputRouter::allowedByPopup(const HitTarget& target)const{
    if(m_popups.empty())
        return !target.popup.widget.valid();
    const PopupRecord& top = m_popups.back();
    return !top.closing && target.popup == top.scope.token;
}

bool InputRouter::popupDescendant(const PopupToken& token, const PopupToken& ancestor)const{
    PopupToken current = token;
    for(usize depth = 0u; depth < s_InputMaxPopups && current.valid(); ++depth){
        if(current == ancestor)
            return true;
        bool found = false;
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

void InputRouter::installPopups(const u64 expectedFocusLossGeneration){
    for(usize index = m_popupDismissals.size(); index > 0u; --index){
        bool retained = false;
        for(const auto& record : m_stagedPopups)
            retained |= record.scope.token == m_popupDismissals[index - 1u].token;
        if(!retained)
            m_popupDismissals.erase(m_popupDismissals.begin() + static_cast<isize>(index - 1u));
    }
    const PopupToken previousTop = m_popups.empty() ? PopupToken{} : m_popups.back().scope.token;
    const PopupToken nextTop = m_stagedPopups.empty() ? PopupToken{} : m_stagedPopups.back().scope.token;
    const auto eligible = [this](const WidgetId id, const u64 declaration, const PopupToken& scope){
        const HitTarget* target = findTarget(id, declaration);
        return __hidden_ui_router_popup::Focusable(target) && target->popup == scope;
    };
    // Rebuild the return chain for the accepted order, including insertions beneath a retained top popup.
    WidgetId candidateFocus = m_popups.empty() ? m_focus : m_popups.front().restoreFocus;
    u64 candidateDeclaration = m_popups.empty() ? m_focusDeclaration : m_popups.front().restoreDeclaration;
    PopupToken candidatePopup;
    const bool lostFocus = !m_windowFocused || (expectedFocusLossGeneration != Limit<u64>::s_Max
        && expectedFocusLossGeneration != m_focusLossGeneration);
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
                if(target.popup == candidatePopup && __hidden_ui_router_popup::Focusable(&target)){
                    candidateFocus = target.id;
                    candidateDeclaration = target.declarationGeneration;
                    break;
                }
            }
        }
    }
    m_popups.swap(m_stagedPopups);
    if(previousTop != nextTop){
        m_focus = candidateFocus;
        m_focusDeclaration = candidateDeclaration;
        const HitTarget* target = findTarget(m_focus, m_focusDeclaration);
        m_focusControl = target ? target->control : ControlToken{};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

