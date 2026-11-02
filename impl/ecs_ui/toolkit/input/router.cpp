// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_router{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TContainer, typename TPredicate>
[[nodiscard]] static auto FindRouterSource(TContainer& sources, TPredicate&& predicate)noexcept -> decltype(&*sources.begin()){
    const auto found = FindIf(sources.begin(), sources.end(), Forward<TPredicate>(predicate));
    return found == sources.end() ? nullptr : &*found;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const HitTarget* InputRouter::FindTargetInTable(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const WidgetId id, const u64 declarationGeneration)noexcept{
    usize targetIndex = 0u;
    if(!FindTargetIndexInTable(targets, lookup, id, declarationGeneration, targetIndex))
        return nullptr;
    return &targets[targetIndex];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputRouter::InputRouter(Core::Alloc::GlobalArena& arena)
    : m_bindings(arena)
    , m_boundSources(arena)
    , m_commandSources(arena)
    , m_pointerBindings(arena)
    , m_pointerSources(arena)
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
    m_boundSources.reserve(s_InputSourceReserve);
    m_commandSources.reserve(s_InputSourceReserve);
    m_pointerBindings.reserve(s_InputSourceReserve);
    m_pointerSources.reserve(s_InputSourceReserve);
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

Expected<InputEvent> InputRouter::queue(const InputEvent& event){
    if(m_events.size() == s_InputMaxEvents || event.type > InputEventType::CommandUp)
        return MakeUnexpected(Failure{});
    const auto pointer = resolvePointerEvent(event);
    if(!pointer)
        return MakeUnexpected(Failure{});
    const auto candidate = resolveSourceEvent(*pointer);
    if(!candidate)
        return MakeUnexpected(Failure{});
    m_events.push_back(*candidate);
    return *candidate;
}

InputRoutingResult InputRouter::process(){
    InputRoutingResult result;
    for(const InputEvent& event : m_events){
        if(IsPointerInputEventType(event.type))
            routePointerSource(event, result);
        else if(event.type == InputEventType::CommandDown || event.type == InputEventType::CommandUp)
            routeCommand(event, result);
        else if(event.type == InputEventType::FocusLost){
            if(m_focusLossGeneration == Limit<u64>::s_Max)
                TerminateInvariant();
            ++m_focusLossGeneration;
            m_windowFocused = false;
            result.pointerConsumed |= hasPopup() || m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed;
            result.commandConsumed |= wantsCommands();
            cancelPopupFocus();
            cancelInteraction();
        }
        else if(event.type == InputEventType::FocusGained)
            m_windowFocused = true;
    }
    m_events.clear();
    result.wantsPointer = wantsPointer();
    result.wantsCommands = wantsCommands();
    result.hover = m_hover;
    result.focus = m_focus;
    result.capture = m_capture;
    return result;
}

b8 InputRouter::commitTargets(const Span<const HitTarget> targets, const u64 layoutGeneration, const Span<const PopupScope> popups, const u64 expectedFocusLossGeneration){
    const usize count = targets.size();
    if(count > s_InputMaxTargets || layoutGeneration <= m_layoutGeneration)
        return false;
    if(!stagePopups(popups))
        return false;
    m_stagedTargets.clear();
    m_stagedLookup.clear();
    for(usize index = 0u; index < count; ++index){
        const HitTarget& target = targets[index];
        if(
            !target.id.valid() || target.declarationGeneration == 0u
            || !IsValidUiRect(target.rectangle)
            || !IsValidUiRect(target.clip)
            || !IsValidUiRect(target.gestureReference)
            || ((target.gestureReference.width == 0.0f) != (target.gestureReference.height == 0.0f))
            || !validPopupTarget(target)
        )
            return false;
        m_stagedTargets.push_back(target);
        m_stagedLookup.push_back({ target.id.value, static_cast<u32>(index) });
    }
    Sort(m_stagedLookup.begin(), m_stagedLookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs)noexcept{
        return lhs.value < rhs.value;
    });
    for(usize index = 1u; index < m_stagedLookup.size(); ++index){
        if(m_stagedLookup[index - 1u].value == m_stagedLookup[index].value)
            return false;
    }
    if(!validControlTargets() || !validCommandOwners())
        return false;
    for(const auto& popup : m_stagedPopups){
        const HitTarget* ownerTarget = FindTargetInTable(m_stagedTargets, m_stagedLookup, popup.scope.token.widget, popup.scope.token.declarationGeneration);
        if(ownerTarget == nullptr || ownerTarget->popup != popup.scope.token || !ownerTarget->enabled)
            return false;
        if(popup.scope.dismissFocusTraversal){
            const HitTarget* anchorTarget = FindTargetInTable(m_stagedTargets, m_stagedLookup, popup.scope.focusAnchor, popup.scope.focusAnchorDeclarationGeneration);
            if(anchorTarget == nullptr || anchorTarget->popup != popup.scope.parent || !anchorTarget->focusable || !anchorTarget->enabled || anchorTarget->owner.valid())
                return false;
        }
    }
    m_targets.swap(m_stagedTargets);
    m_lookup.swap(m_stagedLookup);
    m_layoutGeneration = layoutGeneration;
    installPopups(expectedFocusLossGeneration);
    reconcileTargets();
    if(!m_focus.valid() && m_windowFocused && (expectedFocusLossGeneration == Limit<u64>::s_Max || expectedFocusLossGeneration == m_focusLossGeneration)){
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

void InputRouter::clearFocus()noexcept{
    retireCommandFocus();
    m_focus = {};
    m_focusDeclaration = 0u;
    m_focusControl = {};
}

void InputRouter::reset()noexcept{
    retireQueuedReleases();
    m_events.clear();
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

b8 InputRouter::consumeActivation(const WidgetId id)noexcept{
    const HitTarget* target = findTarget(id);
    if(target == nullptr || !isInteractive(*target) || !target->activatable)
        return false;
    for(usize index = 0u; index < m_actions.size(); ++index){
        const InputActionId& action = m_actions[index].id;
        if(action.target == id && action.declarationGeneration == target->declarationGeneration && m_actions[index].popup == target->popup && m_actions[index].control == target->control){
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
            return true;
        }
    }
    return false;
}

WidgetId InputRouter::hitTest(const Point& position)const noexcept{
    const HitTarget* target = findHitTarget(position);
    return target == nullptr ? WidgetId{} : target->id;
}

b8 InputRouter::wouldConsumePointer(const Point& position)const noexcept{
    if(m_primaryDown)
        return m_pointerSequenceConsumed;
    if(m_secondaryDown)
        return m_secondarySequenceConsumed;
    return hasPopup() || m_capture.valid() || hitTest(position).valid();
}

b8 InputRouter::ownsPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    const PointerSource* held = findPointerSource(source, intent);
    return held != nullptr && held->consumed;
}

b8 InputRouter::ownsSource(const InputSource& source)const noexcept{
    const CommandSource* held = findCommandSource(source);
    return held && held->consumed;
}

PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)noexcept{
    return __hidden_ui_input_router::FindRouterSource(m_pointerSources, [&source, intent](const PointerSource& current)noexcept{
        return current.source == source && current.intent == intent;
    });
}

const PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    return __hidden_ui_input_router::FindRouterSource(m_pointerSources, [&source, intent](const PointerSource& current)noexcept{
        return current.source == source && current.intent == intent;
    });
}

CommandSource* InputRouter::findCommandSource(const InputSource& source)noexcept{
    return __hidden_ui_input_router::FindRouterSource(m_commandSources, [&source](const CommandSource& current)noexcept{
        return current.source == source;
    });
}

const CommandSource* InputRouter::findCommandSource(const InputSource& source)const noexcept{
    return __hidden_ui_input_router::FindRouterSource(m_commandSources, [&source](const CommandSource& current)noexcept{
        return current.source == source;
    });
}

b8 InputRouter::currentCommandFocus(const CommandSource& source)const noexcept{
    const auto& owner = source.focusOwner;
    const HitTarget* focused = findTarget(owner.target, owner.declarationGeneration);
    return
        m_windowFocused && source.consumed && owner.target.valid() && m_focus == owner.target && m_focusDeclaration == owner.declarationGeneration
        && focused != nullptr && focused->focusable && isInteractive(*focused) && focused->popup == owner.popup && focused->control == owner.control
    ;
}

void InputRouter::cancelPointerPress()noexcept{
    m_pointerSubmitArmed = false;
    m_secondaryOwner = {};
    for(usize index = 0u; index < m_pointerGestures.size(); ++index){
        if(m_pointerGestures[index].gesture.id.sequence == m_activeGestureSequence){
            m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
            break;
        }
    }
    m_activeGestureSequence = 0u;
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
}

const HitTarget* InputRouter::findHitTarget(const Point& position)const noexcept{
    if(!IsFinite(position.x) || !IsFinite(position.y))
        return nullptr;
    const HitTarget* found = nullptr;
    for(const HitTarget& target : m_targets){
        if(!UiRectContainsPoint(target.rectangle, position) || !UiRectContainsPoint(target.clip, position) || !isInteractive(target))
            continue;
        if(found == nullptr || target.layer > found->layer || (target.layer == found->layer && target.paintOrder >= found->paintOrder))
            found = &target;
    }
    return found;
}

b8 InputRouter::currentContextMenuOwner(const TargetOwner& owner)const noexcept{
    const HitTarget* target = findTarget(owner.target, owner.declarationGeneration);
    return m_windowFocused && owner.declarationGeneration != 0u && target != nullptr && target->contextMenu && isInteractive(*target) && target->popup == owner.popup && target->control == owner.control;
}

void InputRouter::rebuildLookup(){
    m_lookup.clear();
    for(usize index = 0u; index < m_targets.size(); ++index)
        m_lookup.push_back({ m_targets[index].id.value, static_cast<u32>(index) });
    Sort(m_lookup.begin(), m_lookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs)noexcept{
        return lhs.value < rhs.value;
    });
}

void InputRouter::reconcileTargets()noexcept{
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    const b8 focusedText = focused != nullptr && focused->textEditable && !focused->owner.valid();
    if(focused == nullptr || !isInteractive(*focused) || !focused->focusable || (!focusedText && focused->control != m_focusControl))
        clearFocus();
    else
        m_focusControl = focused->control;
    const HitTarget* captured = findTarget(m_capture, m_captureDeclaration);
    const b8 capturedText = captured != nullptr && captured->textEditable && !captured->owner.valid();
    if(captured == nullptr || !isInteractive(*captured) || captured->popup != m_capturePopup || (!capturedText && captured->control != m_captureControl)){
        m_capture = {};
        m_capturePopup = {};
        m_captureDeclaration = 0u;
        m_captureControl = {};
        m_pointerSubmitArmed = false;
    }
    else{
        // Auxiliary editor controls can change while its main text-selection capture remains current.
        if(captured->control != m_captureControl || !captured->activatable)
            m_pointerSubmitArmed = false;
        m_captureControl = captured->control;
    }
    for(usize index = 0u; index < m_actions.size();){
        const InputActionId& action = m_actions[index].id;
        const HitTarget* target = findTarget(action.target, action.declarationGeneration);
        if(target == nullptr || !isInteractive(*target) || !target->activatable || m_actions[index].popup != target->popup || m_actions[index].control != target->control)
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    reconcilePointerGestures();
    reconcileControlActions();
    reconcileContextMenus();
    reconcileCommandPresses();
    updateHover();
}

void InputRouter::setHoverTarget(const HitTarget* hovered)noexcept{
    const HitTarget* owner = hovered != nullptr && hovered->owner.valid() ? controlHost(*hovered) : hovered;
    HoverIdentity identity;
    if(owner != nullptr)
        identity = { owner->id, owner->declarationGeneration, owner->popup, owner->control };
    if(m_hoverIdentity.owner != identity.owner || m_hoverIdentity.declarationGeneration != identity.declarationGeneration || m_hoverIdentity.popup != identity.popup || m_hoverIdentity.control != identity.control)
        advanceHoverActivity();
    m_hoverIdentity = identity;
    m_hover = hovered == nullptr ? WidgetId{} : hovered->id;
}

void InputRouter::advanceHoverActivity()noexcept{
    if(m_hoverActivityGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_hoverActivityGeneration;
}

void InputRouter::clearHover()noexcept{
    m_hover = {};
    m_hoverIdentity = {};
}

void InputRouter::cancelInteraction()noexcept{
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
    m_secondaryOwner = {};
    m_pointerSubmitArmed = false;
    // Cancel delivery while retaining physical sequence ownership through its matching release.
    retireCommandFocus();
}

void InputRouter::appendActivation(const HitTarget& target, const InputActionSource::Enum origin, const InputSource& inputSource, InputRoutingResult& result){
    if(m_actions.size() == s_InputMaxActions || m_nextActionSequence == 0u){
        result.activationOverflow = true;
        return;
    }
    InputAction action;
    action.id = { target.id, target.declarationGeneration, m_layoutGeneration, m_nextActionSequence };
    action.origin = origin;
    action.popup = target.popup;
    action.control = target.control;
    action.inputSource = inputSource;
    m_actions.push_back(action);
    ++m_nextActionSequence;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

