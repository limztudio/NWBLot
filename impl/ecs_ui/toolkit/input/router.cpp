// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const HitTarget* InputRouter::FindTargetInTable(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const WidgetId id, const u64 declarationGeneration)noexcept{
    const auto targetIndex = FindTargetIndexInTable(targets, lookup, id, declarationGeneration);
    if(!targetIndex)
        return nullptr;
    return &targets[*targetIndex];
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
    if(!StageRouterHitTargets(targets, m_stagedPopups, m_stagedTargets, m_stagedLookup))
        return false;
    Sort(m_stagedLookup.begin(), m_stagedLookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs)noexcept{
        return lhs.value < rhs.value;
    });
    if(!ValidateSortedStagedRouterTargetsAndPopups(m_stagedTargets, m_stagedLookup, m_stagedPopups))
        return false;
    m_targets.swap(m_stagedTargets);
    m_lookup.swap(m_stagedLookup);
    m_layoutGeneration = layoutGeneration;
    installPopups(expectedFocusLossGeneration);
    reconcileTargets();
    ApplyRouterFocusOnCommit(
        m_targets,
        m_lookup,
        m_popups,
        m_windowFocused,
        m_focusLossGeneration,
        expectedFocusLossGeneration,
        m_focus,
        m_focusDeclaration,
        m_focusControl
    );
    return true;
}

void InputRouter::invalidateTarget(const WidgetId id){
    retirePopup(id);
    if(!EraseInvalidatedRouterTargets(id, m_lookup, m_targets))
        return;
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

PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)noexcept{
    const auto foundIndex = FindRouterPointerSourceIndex(m_pointerSources, source, intent);
    return foundIndex ? &m_pointerSources[*foundIndex] : nullptr;
}

const PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    const auto foundIndex = FindRouterPointerSourceIndex(m_pointerSources, source, intent);
    return foundIndex ? &m_pointerSources[*foundIndex] : nullptr;
}

CommandSource* InputRouter::findCommandSource(const InputSource& source)noexcept{
    const auto foundIndex = FindRouterCommandSourceIndex(m_commandSources, source);
    return foundIndex ? &m_commandSources[*foundIndex] : nullptr;
}

const CommandSource* InputRouter::findCommandSource(const InputSource& source)const noexcept{
    const auto foundIndex = FindRouterCommandSourceIndex(m_commandSources, source);
    return foundIndex ? &m_commandSources[*foundIndex] : nullptr;
}

void InputRouter::cancelPointerPress()noexcept{
    CancelRouterPointerPress(
        m_pointerGestures,
        m_activeGestureSequence,
        m_pointerSubmitArmed,
        m_secondaryOwner,
        m_capture,
        m_capturePopup,
        m_captureDeclaration,
        m_captureControl
    );
}

const HitTarget* InputRouter::findHitTarget(const Point& position)const noexcept{
    const auto foundIndex = FindRouterHitTargetIndex(m_targets, m_lookup, m_popups, position);
    return foundIndex ? &m_targets[*foundIndex] : nullptr;
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
    ReconcileRouterFocusCaptureAndActions(
        m_targets,
        m_lookup,
        m_popups,
        m_commandSources,
        m_actions,
        m_focus,
        m_focusDeclaration,
        m_focusControl,
        m_capture,
        m_capturePopup,
        m_captureDeclaration,
        m_captureControl,
        m_pointerSubmitArmed
    );
    reconcilePointerGestures();
    reconcileControlActions();
    reconcileContextMenus();
    reconcileCommandPresses();
    updateHover();
}

void InputRouter::setHoverTarget(const HitTarget* hovered)noexcept{
    b8 changedOwner = false;
    UpdateRouterHoverTarget(
        m_targets,
        m_lookup,
        m_popups,
        hovered != nullptr,
        hovered != nullptr ? *hovered : HitTarget{},
        m_hoverIdentity,
        m_hover,
        changedOwner
    );
    if(changedOwner)
        advanceHoverActivity();
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

void InputRouter::routeCommand(const InputEvent& event, InputRoutingResult& result){
    CommandSource* source = findCommandSource(event.source);
    if(event.type == InputEventType::CommandUp){
        if(source){
            result.commandConsumed |= source->consumed;
            releaseCommand(event, *source, result);
            m_commandSources.erase(m_commandSources.begin() + (source - m_commandSources.data()));
        }
        return;
    }
    const b8 alreadyPressed = source != nullptr;
    if(!source){
        if(m_commandSources.size() == s_InputMaxSources){
            result.activationOverflow = true;
            return;
        }
        CommandSource admitted;
        admitted.source = event.source;
        m_commandSources.push_back(admitted);
        source = &m_commandSources.back();
        const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
        if(m_windowFocused && !event.repeat && focused && isInteractive(*focused))
            source->focusOwner = { focused->id, focused->declarationGeneration, focused->popup, focused->control };
    }
    // Blurred and orphan repeat sequences remain inert until released and pressed again.
    result.commandConsumed |= source->consumed;
    if(!m_windowFocused || (!alreadyPressed && event.repeat))
        return;
    b8 consumed = source->consumed;
    if(alreadyPressed && !consumed)
        return;
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(routeContextMenuCommand(event, focused, alreadyPressed, *source))
        consumed = true;
    else if(event.command == InputCommand::FocusNext || event.command == InputCommand::FocusPrevious)
        consumed |= moveFocusForTraversal(event.command == InputCommand::FocusPrevious) || hasPopup();
    else if(event.command == InputCommand::Cancel){
        consumed |= wantsCommands() || m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed || (!m_pointerSources.empty() && wantsPointer());
        if(!event.repeat && !alreadyPressed){
            cancelCommandPresses();
            cancelPointerPress();
        }
        if(hasPopup() && !(focused && focused->textEditable)){
            const b8 dismissed = !event.repeat && !alreadyPressed && dismissPopup(PopupDismissReason::Cancel);
            if(dismissed)
                updateHover();
            consumed = true;
        }
        else if(!event.repeat && !alreadyPressed && !(focused && focused->textEditable)){
            clearFocus();
        }
    }
    else{
        consumed |= hasPopup() || focused != nullptr;
        RouteRouterActiveCommandAction(
            m_targets,
            m_lookup,
            m_popups,
            event,
            focused != nullptr,
            focused != nullptr ? *focused : HitTarget{},
            alreadyPressed,
            m_layoutGeneration,
            *source,
            m_controlActions,
            m_nextActionSequence,
            result
        );
    }
    if(consumed){
        source->consumed = true;
        result.commandConsumed = true;
    }
}

void InputRouter::retireQueuedReleases()noexcept{
    b8 unfinishedHoverSource = false;
    RetireRouterQueuedReleases(
        m_events,
        m_hoverPointerSource,
        m_commandSources,
        m_pointerSources,
        m_pointerGestures,
        m_activeGestureSequence,
        m_pointerSubmitArmed,
        m_secondaryOwner,
        m_capture,
        m_capturePopup,
        m_captureDeclaration,
        m_captureControl,
        m_primaryDown,
        m_pointerSequenceConsumed,
        m_secondaryDown,
        m_secondarySequenceConsumed,
        m_activePointerSource,
        unfinishedHoverSource
    );
    if(unfinishedHoverSource){
        advanceHoverActivity();
        clearHover();
        m_pointerKnown = false;
        m_hoverPointerSource = {};
    }
}

void InputRouter::routePointerSource(const InputEvent& event, InputRoutingResult& result){
    if(event.type == InputEventType::PointerCaptureLost){
        result.pointerConsumed |= ownsPointerSource(event.source, PointerIntent::Activate) || ownsPointerSource(event.source, PointerIntent::ContextMenu);
        result.pointerRouted |= m_activePointerSource == event.source;
        terminatePointerSource(event.source);
        return;
    }
    if(event.type == InputEventType::PointerLeave){
        result.pointerConsumed |= ownsPointerSource(event.source, PointerIntent::Activate) || ownsPointerSource(event.source, PointerIntent::ContextMenu);
        result.pointerRouted |= m_activePointerSource == event.source;
        if(m_hoverPointerSource == event.source){
            advanceHoverActivity();
            m_pointerKnown = false;
            m_hoverPointerSource = {};
            clearHover();
        }
        return;
    }
    const b8 press = event.type == InputEventType::PrimaryDown || event.type == InputEventType::SecondaryDown;
    const b8 release = event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp;
    // Reuse only within this event; popup dismissal and layout reconciliation still resolve hover against their new state.
    const HitTarget* hit = event.hoverCapable ? findHitTarget(event.position) : nullptr;
    b8 hitResolved = event.hoverCapable;
    const auto resolveHit = [this, &event, &hit, &hitResolved]()noexcept{
        if(!hitResolved){
            hit = findHitTarget(event.position);
            hitResolved = true;
        }
        return hit;
    };
    observePointer(event, hit);
    const PointerIntent::Enum intent = event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp ? PointerIntent::ContextMenu : PointerIntent::Activate;
    PointerSource* held = findPointerSource(event.source, intent);
    if(press){
        if(held != nullptr){
            result.pointerConsumed |= held->consumed;
            return;
        }
        if(m_pointerSources.size() == s_InputMaxSources)
            return;
        const b8 accepted = !event.repeat && acceptsPointerSource(event.source);
        const b8 consumed = !event.repeat && (m_activePointerSource == event.source ? m_primaryDown ? m_pointerSequenceConsumed : m_secondarySequenceConsumed : hasPopup() || resolveHit() != nullptr);
        m_pointerSources.push_back({ event.source, intent, consumed, !accepted });
        result.pointerConsumed |= consumed;
        if(!accepted)
            return;
        m_activePointerSource = event.source;
    }
    else if(release){
        if(held == nullptr){
            result.pointerConsumed |= hasPopup() || resolveHit() != nullptr;
            return;
        }
        result.pointerConsumed |= held->consumed;
        if(held->inert || m_activePointerSource != event.source){
            const auto index = static_cast<isize>(held - m_pointerSources.data());
            m_pointerSources.erase(m_pointerSources.begin() + index);
            return;
        }
    }
    else{
        const PointerSource* activation = findPointerSource(event.source, PointerIntent::Activate);
        const PointerSource* context = findPointerSource(event.source, PointerIntent::ContextMenu);
        const b8 blocked = (activation != nullptr && activation->inert) || (context != nullptr && context->inert) || !acceptsPointerSource(event.source);
        if(blocked){
            result.pointerConsumed |= (activation != nullptr && activation->consumed) || (context != nullptr && context->consumed);
            return;
        }
        if(!m_activePointerSource.valid() && !event.hoverCapable && event.type == InputEventType::PointerMove)
            return;
    }
    if(press || release || event.type == InputEventType::PointerWheel)
        resolveHit();
    result.pointerRouted = true;
    m_interactionPosition = event.position;
    if(event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp)
        routeSecondary(event, hit, result);
    else if(event.type == InputEventType::PointerWheel)
        routeWheel(event, hit, result);
    else
        routePointer(event, hit, result);
    if(release){
        const auto index = static_cast<isize>(held - m_pointerSources.data());
        m_pointerSources.erase(m_pointerSources.begin() + index);
        if(!m_primaryDown && !m_secondaryDown)
            m_activePointerSource = {};
    }
}

void InputRouter::observePointer(const InputEvent& event, const HitTarget* hit)noexcept{
    if(!event.hoverCapable)
        return;
    m_hoverPointerSource = event.source;
    m_pointer = event.position;
    m_pointerKnown = true;
    setHoverTarget(hit);
}

void InputRouter::terminatePointerSource(const InputSource source)noexcept{
    b8 unfinishedHoverSource = false;
    TerminateRouterPointerSource(
        source,
        m_hoverPointerSource,
        m_pointerSources,
        m_pointerGestures,
        m_activeGestureSequence,
        m_pointerSubmitArmed,
        m_secondaryOwner,
        m_capture,
        m_capturePopup,
        m_captureDeclaration,
        m_captureControl,
        m_primaryDown,
        m_pointerSequenceConsumed,
        m_secondaryDown,
        m_secondarySequenceConsumed,
        m_activePointerSource,
        unfinishedHoverSource
    );
    if(unfinishedHoverSource){
        advanceHoverActivity();
        clearHover();
        m_pointerKnown = false;
        m_hoverPointerSource = {};
    }
}

void InputRouter::routePointer(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
    if(event.type == InputEventType::PointerMove){
        if(m_primaryDown)
            updatePointerGesture(event.position, false);
        result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        return;
    }
    if(event.type == InputEventType::PrimaryDown){
        if(m_primaryDown){
            result.pointerConsumed |= m_pointerSequenceConsumed;
            return;
        }
        advanceHoverActivity();
        m_primaryDown = true;
        m_pointerSubmitArmed = false;
        m_pointerSequenceConsumed = m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        result.pointerConsumed |= m_pointerSequenceConsumed;
        if(!m_pointerSequenceConsumed){
            clearFocus();
            return;
        }
        if(hasPopup() && hit == nullptr){
            const b8 dismissed = dismissPopup(PopupDismissReason::OutsideClick);
            if(dismissed)
                updateHover();
            m_capture = {};
            m_capturePopup = {};
            m_captureDeclaration = 0u;
            m_captureControl = {};
            return;
        }
        m_capture = hit == nullptr ? WidgetId{} : hit->id;
        m_captureDeclaration = hit == nullptr ? 0u : hit->declarationGeneration;
        m_capturePopup = hit == nullptr ? PopupToken{} : hit->popup;
        m_captureControl = hit == nullptr ? ControlToken{} : hit->control;
        m_pointerSubmitArmed = hit != nullptr && hit->activatable;
        const HitTarget* focused = hit != nullptr && hit->owner.valid() ? controlHost(*hit) : hit;
        if(focused != nullptr && focused->focusable)
            assignFocus(*focused);
        else
            clearFocus();
        if(hit != nullptr && hit->pointerGesture)
            appendPointerGesture(*hit, result);
        return;
    }
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hit != nullptr;
    if(m_primaryDown && m_pointerSequenceConsumed && m_pointerSubmitArmed && hit != nullptr && hit->id == m_capture && hit->declarationGeneration == m_captureDeclaration && hit->activatable && hit->popup == m_capturePopup && hit->control == m_captureControl){
        const HitTarget* host = hit->owner.valid() ? controlHost(*hit) : nullptr;
        if(host != nullptr)
            appendControlAction(*host, *hit, ControlActionKind::Activate, 0.0, result);
        else
            appendActivation(*hit, InputActionSource::Pointer, event.source, result);
    }
    if(m_primaryDown)
        updatePointerGesture(event.position, true);
    m_primaryDown = false;
    m_pointerSubmitArmed = false;
    m_pointerSequenceConsumed = false;
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
}

void InputRouter::routeSecondary(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
    b8 advanceHover = false;
    b8 dismissedPopup = false;
    RouteRouterSecondary(
        m_targets,
        m_lookup,
        m_windowFocused,
        m_layoutGeneration,
        event,
        hit != nullptr,
        hit != nullptr ? *hit : HitTarget{},
        m_primaryDown,
        m_pointerSequenceConsumed,
        m_popups,
        m_popupDismissals,
        m_contextMenuActions,
        m_nextActionSequence,
        m_secondaryDown,
        m_secondarySequenceConsumed,
        m_secondaryOwner,
        m_secondaryPosition,
        result,
        advanceHover,
        dismissedPopup
    );
    if(advanceHover)
        advanceHoverActivity();
    if(dismissedPopup){
        reconcileTargets();
        updateHover();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

