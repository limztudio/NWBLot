// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_router{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidRectangle(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

bool Contains(const Rect& rectangle, const Point& position){
    return
        position.x >= rectangle.x && position.y >= rectangle.y
        && position.x < rectangle.x + rectangle.width && position.y < rectangle.y + rectangle.height
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputRouter::InputRouter(Core::Alloc::GlobalArena& arena)
    : m_bindings(arena)
    , m_boundSources(arena)
    , m_commandSources(arena)
    , m_targets(arena)
    , m_stagedTargets(arena)
    , m_lookup(arena)
    , m_stagedLookup(arena)
    , m_events(arena)
    , m_actions(arena)
    , m_controlActions(arena)
    , m_contextMenuActions(arena)
    , m_pointerGestures(arena)
    , m_popups(arena)
    , m_stagedPopups(arena)
    , m_popupDismissals(arena)
{
    m_boundSources.reserve(16u);
    m_commandSources.reserve(16u);
    m_targets.reserve(s_InputMaxTargets);
    m_stagedTargets.reserve(s_InputMaxTargets);
    m_lookup.reserve(s_InputMaxTargets);
    m_stagedLookup.reserve(s_InputMaxTargets);
    m_events.reserve(s_InputMaxEvents);
    m_actions.reserve(s_InputMaxActions);
    m_controlActions.reserve(s_InputMaxControlActions);
    m_contextMenuActions.reserve(s_InputMaxContextMenuActions);
    m_pointerGestures.reserve(s_InputMaxPointerGestures);
    m_popups.reserve(s_InputMaxPopups);
    m_stagedPopups.reserve(s_InputMaxPopups);
    m_popupDismissals.reserve(s_InputMaxPopups);
}

bool InputRouter::queue(const InputEvent& event, InputEvent* const resolved){
    if(m_events.size() == s_InputMaxEvents || event.type > InputEventType::CommandUp)
        return false;
    if(
        (event.type <= InputEventType::PrimaryUp || event.type == InputEventType::PointerWheel
            || event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp)
        && (!IsFinite(event.position.x) || !IsFinite(event.position.y))
    )
        return false;
    if(event.type == InputEventType::PointerWheel && (!IsFinite(event.scrollX) || !IsFinite(event.scrollY)))
        return false;
    InputEvent candidate;
    if(!resolveSourceEvent(event, candidate))
        return false;
    m_events.push_back(candidate);
    if(resolved)
        *resolved = candidate;
    return true;
}

InputRoutingResult InputRouter::process(){
    InputRoutingResult result;
    for(const InputEvent& event : m_events){
        if(event.type <= InputEventType::PrimaryUp)
            routePointer(event, result);
        else if(event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp)
            routeSecondary(event, result);
        else if(event.type == InputEventType::PointerWheel)
            routeWheel(event, result);
        else if(event.type == InputEventType::CommandDown || event.type == InputEventType::CommandUp)
            routeCommand(event, result);
        else if(event.type == InputEventType::FocusLost){
            if(m_focusLossGeneration == Limit<u64>::s_Max)
                TerminateInvariant();
            ++m_focusLossGeneration;
            m_windowFocused = false;
            result.pointerConsumed |= hasPopup() || m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed;
            result.keyboardConsumed |= wantsKeyboard();
            cancelPopupFocus();
            cancelInteraction();
        }
        else if(event.type == InputEventType::FocusGained)
            m_windowFocused = true;
        else if(event.type == InputEventType::PointerCaptureLost){
            result.pointerConsumed |= m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed;
            cancelPointerCapture();
        }
        else{
            result.pointerConsumed |= m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed;
            advanceHoverActivity();
            m_pointerKnown = false;
            clearHover();
        }
    }
    m_events.clear();
    result.wantsPointer = wantsPointer();
    result.wantsKeyboard = wantsKeyboard();
    result.hover = m_hover;
    result.focus = m_focus;
    result.capture = m_capture;
    return result;
}

bool InputRouter::commitTargets(
    const HitTarget* targets, const usize count, const u64 layoutGeneration, const PopupScope* popups,
    const usize popupCount, const u64 expectedFocusLossGeneration){
    if(count > s_InputMaxTargets || (count != 0u && targets == nullptr) || layoutGeneration == 0u || layoutGeneration <= m_layoutGeneration)
        return false;
    if(!stagePopups(popups, popupCount))
        return false;
    m_stagedTargets.clear();
    m_stagedLookup.clear();
    for(usize index = 0u; index < count; ++index){
        const HitTarget& target = targets[index];
        if(
            !target.id.valid() || target.declarationGeneration == 0u
            || !__hidden_ui_input_router::ValidRectangle(target.rectangle)
            || !__hidden_ui_input_router::ValidRectangle(target.clip)
            || !__hidden_ui_input_router::ValidRectangle(target.gestureReference)
            || ((target.gestureReference.width == 0.0f) != (target.gestureReference.height == 0.0f))
            || !validPopupTarget(target)
        )
            return false;
        m_stagedTargets.push_back(target);
        m_stagedLookup.push_back({ target.id.value, static_cast<u32>(index) });
    }
    Sort(m_stagedLookup.begin(), m_stagedLookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs){
        return lhs.value < rhs.value;
    });
    for(usize index = 1u; index < m_stagedLookup.size(); ++index){
        if(m_stagedLookup[index - 1u].value == m_stagedLookup[index].value)
            return false;
    }
    if(!validControlTargets() || !validKeyboardOwners())
        return false;
    for(const auto& popup : m_stagedPopups){
        bool owner = false;
        for(const auto& target : m_stagedTargets){
            owner |= target.id == popup.scope.token.widget && target.declarationGeneration == popup.scope.token.declarationGeneration
                && target.popup == popup.scope.token && target.enabled;
        }
        if(!owner)
            return false;
        if(popup.scope.dismissFocusTraversal){
            bool anchor = false;
            for(const auto& target : m_stagedTargets){
                anchor |= target.id == popup.scope.focusAnchor
                    && target.declarationGeneration == popup.scope.focusAnchorDeclarationGeneration
                    && target.popup == popup.scope.parent && target.focusable && target.enabled && !target.owner.valid();
            }
            if(!anchor)
                return false;
        }
    }
    m_targets.swap(m_stagedTargets);
    m_lookup.swap(m_stagedLookup);
    m_layoutGeneration = layoutGeneration;
    installPopups(expectedFocusLossGeneration);
    reconcileTargets();
    if(
        !m_focus.valid() && m_windowFocused
        && (expectedFocusLossGeneration == Limit<u64>::s_Max || expectedFocusLossGeneration == m_focusLossGeneration)
    ){
        for(const auto& target : m_targets){
            if(target.focusOnCommit && target.focusable && isInteractive(target)){
                m_focus = target.id;
                m_focusDeclaration = target.declarationGeneration;
                m_focusControl = target.control;
                break;
            }
        }
    }
    return true;
}

const HitTarget* InputRouter::findTarget(const WidgetId id, const u64 declarationGeneration)const{
    usize begin = 0u;
    usize end = m_lookup.size();
    while(begin < end){
        const usize middle = begin + (end - begin) / 2u;
        if(m_lookup[middle].value < id.value)
            begin = middle + 1u;
        else
            end = middle;
    }
    if(begin == m_lookup.size() || m_lookup[begin].value != id.value)
        return nullptr;
    const HitTarget& target = m_targets[m_lookup[begin].index];
    return declarationGeneration == 0u || target.declarationGeneration == declarationGeneration ? &target : nullptr;
}

void InputRouter::invalidateTarget(const WidgetId id){
    retirePopup(id);
    if(!findTarget(id))
        return;
    for(usize index = m_targets.size(); index > 0u; --index){
        if(m_targets[index - 1u].id == id || m_targets[index - 1u].owner == id)
            m_targets.erase(m_targets.begin() + static_cast<isize>(index - 1u));
    }
    rebuildLookup();
    reconcileTargets();
}

void InputRouter::rebuildLookup(){
    m_lookup.clear();
    for(usize index = 0u; index < m_targets.size(); ++index)
        m_lookup.push_back({ m_targets[index].id.value, static_cast<u32>(index) });
    Sort(m_lookup.begin(), m_lookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs){
        return lhs.value < rhs.value;
    });
}

void InputRouter::clearFocus(){
    m_focus = {};
    m_focusDeclaration = 0u;
    m_focusControl = {};
}

void InputRouter::reset(){
    m_events.clear();
    m_boundSources.clear();
    m_targets.clear();
    m_stagedTargets.clear();
    m_lookup.clear();
    m_stagedLookup.clear();
    m_popups.clear();
    m_stagedPopups.clear();
    m_popupDismissals.clear();
    m_layoutGeneration = 0u;
    cancelInteraction();
}

bool InputRouter::consumeActivation(const WidgetId id){
    const HitTarget* target = findTarget(id);
    if(target == nullptr || !isInteractive(*target) || !target->activatable)
        return false;
    for(usize index = 0u; index < m_actions.size(); ++index){
        const InputActionId& action = m_actions[index].id;
        if(
            action.target == id && action.declarationGeneration == target->declarationGeneration
            && m_actions[index].popup == target->popup && m_actions[index].control == target->control
        ){
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
            return true;
        }
    }
    return false;
}

WidgetId InputRouter::hitTest(const Point& position)const{
    const HitTarget* target = findHitTarget(position);
    return target == nullptr ? WidgetId{} : target->id;
}

bool InputRouter::wouldConsumePointer(const Point& position)const{
    if(m_primaryDown)
        return m_pointerSequenceConsumed;
    if(m_secondaryDown)
        return m_secondarySequenceConsumed;
    return hasPopup() || m_capture.valid() || hitTest(position).valid();
}

const HitTarget* InputRouter::findHitTarget(const Point& position)const{
    if(!IsFinite(position.x) || !IsFinite(position.y))
        return nullptr;
    const HitTarget* found = nullptr;
    for(const HitTarget& target : m_targets){
        if(
            !isInteractive(target)
            || !__hidden_ui_input_router::Contains(target.rectangle, position)
            || !__hidden_ui_input_router::Contains(target.clip, position)
        )
            continue;
        if(found == nullptr || target.layer > found->layer || (target.layer == found->layer && target.paintOrder >= found->paintOrder))
            found = &target;
    }
    return found;
}

bool InputRouter::isInteractive(const HitTarget& target)const{
    const f32 left = Max(target.rectangle.x, target.clip.x);
    const f32 top = Max(target.rectangle.y, target.clip.y);
    const f32 right = Min(target.rectangle.x + target.rectangle.width, target.clip.x + target.clip.width);
    const f32 bottom = Min(target.rectangle.y + target.rectangle.height, target.clip.y + target.clip.height);
    return
        target.enabled && right > left && bottom > top && allowedByPopup(target)
        && (!target.owner.valid() || controlHost(target) != nullptr)
    ;
}

void InputRouter::reconcileTargets(){
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    const bool focusedText = focused != nullptr && focused->textEditable && !focused->owner.valid();
    if(
        focused == nullptr || !isInteractive(*focused) || !focused->focusable
        || (!focusedText && focused->control != m_focusControl)
    ){
        m_focus = {};
        m_focusDeclaration = 0u;
        m_focusControl = {};
    }
    else
        m_focusControl = focused->control;
    const HitTarget* captured = findTarget(m_capture, m_captureDeclaration);
    const bool capturedText = captured != nullptr && captured->textEditable && !captured->owner.valid();
    if(
        captured == nullptr || !isInteractive(*captured) || captured->popup != m_capturePopup
        || (!capturedText && captured->control != m_captureControl)
    ){
        m_capture = {};
        m_capturePopup = {};
        m_captureDeclaration = 0u;
        m_captureControl = {};
    }
    else
        m_captureControl = captured->control;
    for(usize index = 0u; index < m_actions.size();){
        const InputActionId& action = m_actions[index].id;
        const HitTarget* target = findTarget(action.target, action.declarationGeneration);
        if(
            target == nullptr || !isInteractive(*target) || !target->activatable
            || m_actions[index].popup != target->popup || m_actions[index].control != target->control
        )
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    reconcilePointerGestures();
    reconcileControlActions();
    reconcileContextMenus();
    updateHover();
}

void InputRouter::updateHover(){
    const HitTarget* hovered = m_pointerKnown ? findHitTarget(m_pointer) : nullptr;
    const HitTarget* owner = hovered != nullptr && hovered->owner.valid() ? controlHost(*hovered) : hovered;
    HoverIdentity identity;
    if(owner != nullptr)
        identity = { owner->id, owner->declarationGeneration, owner->popup, owner->control };
    if(
        m_hoverIdentity.owner != identity.owner || m_hoverIdentity.declarationGeneration != identity.declarationGeneration
        || m_hoverIdentity.popup != identity.popup || m_hoverIdentity.control != identity.control
    )
        advanceHoverActivity();
    m_hoverIdentity = identity;
    m_hover = hovered == nullptr ? WidgetId{} : hovered->id;
}

void InputRouter::advanceHoverActivity(){
    if(m_hoverActivityGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_hoverActivityGeneration;
}

void InputRouter::clearHover(){
    m_hover = {};
    m_hoverIdentity = {};
}

void InputRouter::cancelPointerCapture(){
    advanceHoverActivity();
    const bool secondaryHeld = m_secondaryDown;
    m_secondaryDown = false;
    m_secondarySequenceConsumed = false;
    m_secondaryOwner = {};
    if(!m_primaryDown){
        if(secondaryHeld){
            m_pointerKnown = false;
            clearHover();
        }
        return;
    }
    for(usize index = 0u; index < m_pointerGestures.size(); ++index){
        if(m_pointerGestures[index].gesture.id.sequence == m_activeGestureSequence){
            m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
            break;
        }
    }
    m_activeGestureSequence = 0u;
    clearHover();
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
    m_pointerKnown = false;
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
}

void InputRouter::cancelInteraction(){
    advanceHoverActivity();
    m_actions.clear();
    m_controlActions.clear();
    m_contextMenuActions.clear();
    m_pointerGestures.clear();
    m_activeGestureSequence = 0u;
    clearHover();
    m_focus = {};
    m_capture = {};
    m_capturePopup = {};
    m_captureControl = {};
    m_focusControl = {};
    m_focusDeclaration = 0u;
    m_captureDeclaration = 0u;
    m_pointerKnown = false;
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
    m_secondaryDown = false;
    m_secondarySequenceConsumed = false;
    m_secondaryOwner = {};
    m_commandSources.clear();
}

void InputRouter::appendActivation(const HitTarget& target, const InputActionSource::Enum source, InputRoutingResult& result){
    if(m_actions.size() == s_InputMaxActions || m_nextActionSequence == 0u){
        result.activationOverflow = true;
        return;
    }
    m_actions.push_back({
        { target.id, target.declarationGeneration, m_layoutGeneration, m_nextActionSequence }, source, target.popup, target.control
    });
    ++m_nextActionSequence;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

