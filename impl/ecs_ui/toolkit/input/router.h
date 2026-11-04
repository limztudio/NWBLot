// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bindings.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <impl/ecs_ui/toolkit/id.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>

#include <global/algorithm.h>
#include <global/assert.h>
#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using InputVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A single CPU owner serializes input and layout publication; native capture, IME, and clipboard remain OS services.
class InputRouter final : NoCopy{
private:
    [[nodiscard]] static const HitTarget* FindTargetInTable(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const WidgetId id, const u64 declarationGeneration = 0u)noexcept{
        const auto targetIndex = FindTargetIndexInTable(targets, lookup, id, declarationGeneration);
        return targetIndex ? &targets[*targetIndex] : nullptr;
    }
    [[nodiscard]] static b8 HasVisibleBounds(const HitTarget& target)noexcept{
        return HasVisibleUiRectIntersection(target.rectangle, target.clip);
    }


public:
    explicit InputRouter(Core::Alloc::GlobalArena& arena)
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


public:
    [[nodiscard]] Expected<InputEvent> queue(const InputEvent& event){
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
    [[nodiscard]] Expected<InputEvent> queuePointer(const PointerInput& input){
        const auto event = TryMakePointerInputEvent(input);
        if(!event)
            return MakeUnexpected(Failure{});
        return queue(*event);
    }
    [[nodiscard]] b8 setBindings(const Span<const InputKeyBinding> bindings){ return m_bindings.set(bindings); }
    void restoreDefaultBindings(){ m_bindings.restoreDefaults(); }

    [[nodiscard]] const InputBindings& bindings()const noexcept{ return m_bindings; }

    // Drain events exactly once against the current committed layout; pending actions survive subsequent process calls.
    [[nodiscard]] InputRoutingResult process(){
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
    // Failure preserves the committed layout and all interaction state; generations increase until reset.
    // Context passes the focus-loss generation captured at preparation to fence delayed popup acceptance.
    [[nodiscard]] b8 commitTargets(const Span<const HitTarget> targets, const u64 layoutGeneration, const Span<const PopupScope> popups = {}, const u64 expectedFocusLossGeneration = Limit<u64>::s_Max){
        if(targets.size() > s_InputMaxTargets || layoutGeneration <= m_layoutGeneration)
            return false;
        if(!stagePopups(popups) || !StageRouterHitTargets(targets, m_stagedPopups, m_stagedTargets, m_stagedLookup))
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
    [[nodiscard]] Expected<PopupDismissReason::Enum> consumePopupDismissal(const PopupToken& token)noexcept{
        return TryConsumeRouterPopupDismissal(token, m_popupDismissals);
    }
    [[nodiscard]] b8 dismissPopup(const PopupDismissReason::Enum reason){
        if(!TryDismissRouterPopup(reason, m_popups, m_popupDismissals))
            return false;
        reconcileTargets();
        return true;
    }
    void closePopup(const PopupToken& token)noexcept{
        CloseRouterPopupSubtree(token, m_popups);
        reconcileTargets();
    }
    void fencePopup(const PopupToken& token)noexcept{
        FenceRouterPopup(token, m_popups);
        reconcileTargets();
    }

    [[nodiscard]] b8 hasPopup()const noexcept{ return !m_popups.empty(); }

    [[nodiscard]] usize popupCount()const noexcept{ return m_popups.size(); }

    // Accepted observations are borrowed only until layout publication, reset, or declaration retirement.
    [[nodiscard]] const PopupScope* popupScope(const PopupToken& token)const noexcept{
        const auto scopeIndex = FindRouterPopupScopeIndex(m_popups, token);
        return scopeIndex ? &m_popups[*scopeIndex].scope : nullptr;
    }
    [[nodiscard]] const HitTarget* findTarget(const WidgetId id, const u64 declarationGeneration = 0u)const noexcept{
        return FindTargetInTable(m_targets, m_lookup, id, declarationGeneration);
    }

    [[nodiscard]] PopupToken topPopupToken()const noexcept{ return m_popups.empty() ? PopupToken{} : m_popups.back().scope.token; }

    [[nodiscard]] u64 focusLossGeneration()const noexcept{ return m_focusLossGeneration; }

    // Remove a hidden/deleted declaration immediately while a prior GPU frame remains pending.
    void invalidateTarget(const WidgetId id){
        retirePopup(id);
        if(!EraseInvalidatedRouterTargets(id, m_lookup, m_targets))
            return;
        rebuildLookup();
        reconcileTargets();
    }
    // Transfer command focus to another input owner without cancelling already accepted UI actions.
    void clearFocus()noexcept{
        retireCommandFocus();
        m_focus = {};
        m_focusDeclaration = 0u;
        m_focusControl = {};
    }
    // Invalidate interaction and layout after resize/device/root changes; action sequences never restart.
    void reset()noexcept{
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
    // Device termination may suppress releases; only this device's pending sources are removed.
    void cancelDeviceInput(const u64 device)noexcept{
        if(m_activePointerSource.valid() && m_activePointerSource.device == device)
            terminatePointerSource(m_activePointerSource);
        EraseRouterDeviceInputs(device, m_pointerSources, m_pointerBindings, m_commandSources, m_boundSources, m_events);
        if(m_hoverPointerSource.valid() && m_hoverPointerSource.device == device){
            advanceHoverActivity();
            clearHover();
            m_pointerKnown = false;
            m_hoverPointerSource = {};
        }
    }
    [[nodiscard]] b8 consumeActivation(const WidgetId id)noexcept{
        return TryConsumeRouterActivation(m_targets, m_lookup, m_popups, id, m_actions);
    }
    [[nodiscard]] Expected<ContextMenuAction> consumeContextMenu(const WidgetId id, const u64 declarationGeneration)noexcept{
        return TryConsumeRouterContextMenu(m_targets, m_lookup, m_popups, m_windowFocused, id, declarationGeneration, m_contextMenuActions);
    }
    [[nodiscard]] Expected<ControlAction> consumeControlAction(const WidgetId host, const u64 declarationGeneration, const ControlToken& token)noexcept{
        return TryConsumeRouterControlAction(m_targets, m_lookup, m_popups, host, declarationGeneration, token, m_controlActions);
    }
    // A replaced borrowed model cannot receive input from its still displayed earlier lifetime.
    void fenceControl(const WidgetId host, const u64 declarationGeneration, const ControlToken& token)noexcept{
        if(FenceRouterControlTargets(host, declarationGeneration, token, m_lookup, m_targets))
            reconcileTargets();
    }
    // Active updates are consumed once while retaining their press baseline; terminal updates survive until consumed.
    [[nodiscard]] Expected<PointerGesture> consumePointerGesture(const WidgetId id, const u64 declarationGeneration)noexcept{
        return TryConsumeRouterPointerGesture(m_targets, m_lookup, m_popups, id, declarationGeneration, m_pointerGestures);
    }
    [[nodiscard]] WidgetId hitTest(const Point& position)const noexcept{
        const HitTarget* target = findHitTarget(position);
        return target == nullptr ? WidgetId{} : target->id;
    }
    [[nodiscard]] b8 wouldConsumePointer(const Point& position)const noexcept{
        if(m_primaryDown)
            return m_pointerSequenceConsumed;
        if(m_secondaryDown)
            return m_secondarySequenceConsumed;
        return hasPopup() || m_capture.valid() || hitTest(position).valid();
    }

    [[nodiscard]] u64 layoutGeneration()const noexcept{ return m_layoutGeneration; }

    [[nodiscard]] const InputVector<HitTarget>& targets()const noexcept{ return m_targets; }

    [[nodiscard]] const InputVector<InputAction>& actions()const noexcept{ return m_actions; }

    [[nodiscard]] const InputVector<ControlAction>& controlActions()const noexcept{ return m_controlActions; }

    [[nodiscard]] WidgetId hover()const noexcept{ return m_hover; }

    [[nodiscard]] u64 hoverActivityGeneration()const noexcept{ return m_hoverActivityGeneration; }

    [[nodiscard]] WidgetId focus()const noexcept{ return m_focus; }

    [[nodiscard]] WidgetId capture()const noexcept{ return m_capture; }

    // One spatial source can own activation and context-menu roles concurrently; other held contacts remain inert.
    [[nodiscard]] InputSource activePointerSource()const noexcept{ return m_activePointerSource; }

    [[nodiscard]] b8 acceptsPointerSource(const InputSource& source)const noexcept{
        return AcceptsRouterPointerSource(m_activePointerSource, m_pointerSources, source);
    }
    [[nodiscard]] b8 ownsPointerSource(const InputSource& source, const PointerIntent::Enum intent = PointerIntent::Activate)const noexcept{
        return OwnsRouterPointerSource(m_pointerSources, source, intent);
    }

    [[nodiscard]] b8 primaryDown()const noexcept{ return m_primaryDown; }

    [[nodiscard]] b8 secondaryDown()const noexcept{ return m_secondaryDown; }

    [[nodiscard]] b8 pointerKnown()const noexcept{ return m_pointerKnown; }

    // Only an explicitly hover-capable source supplies cursor geometry; contact gestures keep their own position.
    [[nodiscard]] const Point& pointerPosition()const noexcept{ return m_pointer; }

    [[nodiscard]] b8 windowFocused()const noexcept{ return m_windowFocused; }

    [[nodiscard]] b8 wantsPointer()const noexcept{
        return WantsRouterPointer(m_pointerSources, hasPopup(), m_hover);
    }
    [[nodiscard]] b8 wantsCommands()const noexcept{
        return WantsRouterCommands(m_popups, m_focus, m_commandSources);
    }
    [[nodiscard]] b8 ownsKey(const i32 key)const noexcept{
        return InputBindings::ValidKey(key) && ownsSource({ 0u, static_cast<u64>(key) + 1u });
    }
    [[nodiscard]] b8 ownsSource(const InputSource& source)const noexcept{
        return OwnsRouterCommandSource(m_commandSources, source);
    }
    // Held commands cannot switch editors or begin editing after their delegated owner has retired.
    [[nodiscard]] b8 canEditCommand(const InputEvent& event)const noexcept{
        return CanRouterEditCommand(m_targets, m_lookup, m_popups, m_commandSources, m_windowFocused, m_focus, m_focusDeclaration, event);
    }
    // Query a queued release before process retires its physical source.
    [[nodiscard]] b8 canSubmitCommand(const InputEvent& event)const noexcept{
        return CanRouterSubmitCommand(m_targets, m_lookup, m_popups, m_commandSources, m_windowFocused, m_focus, m_focusDeclaration, event);
    }
    void cancelSubmitCommands(const WidgetId id)noexcept{
        CancelRouterSubmitCommands(id, m_commandSources);
    }
    [[nodiscard]] b8 pressed(const WidgetId id)const noexcept{
        if(m_primaryDown && m_pointerSubmitArmed && m_capture == id && hitTest(m_interactionPosition) == id)
            return true;
        return IsRouterTargetPressedByCommand(m_targets, m_lookup, m_popups, m_commandSources, m_windowFocused, m_focus, m_focusDeclaration, id);
    }


private:
    [[nodiscard]] Expected<InputEvent> resolveSourceEvent(const InputEvent& event){
        return TryResolveRouterSourceEvent(m_bindings.bindings(), event, m_boundSources);
    }
    [[nodiscard]] Expected<InputEvent> resolvePointerEvent(const InputEvent& event){
        return TryResolveRouterPointerEvent(event, m_pointerBindings);
    }
    void retireQueuedReleases()noexcept{
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
    [[nodiscard]] PointerSource* findPointerSource(const InputSource& source, const PointerIntent::Enum intent)noexcept{
        const auto found = FindRouterPointerSourceIndex(m_pointerSources, source, intent);
        return found ? &m_pointerSources[*found] : nullptr;
    }
    [[nodiscard]] const PointerSource* findPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
        const auto found = FindRouterPointerSourceIndex(m_pointerSources, source, intent);
        return found ? &m_pointerSources[*found] : nullptr;
    }
    void routePointerSource(const InputEvent& event, InputRoutingResult& result){
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
    void observePointer(const InputEvent& event, const HitTarget* const hit)noexcept{
        b8 changedOwner = false;
        ObserveRouterPointer(
            m_targets,
            m_lookup,
            m_popups,
            event,
            hit != nullptr,
            hit != nullptr ? *hit : HitTarget{},
            m_pointer,
            m_pointerKnown,
            m_hoverPointerSource,
            m_hoverIdentity,
            m_hover,
            changedOwner
        );
        if(changedOwner)
            advanceHoverActivity();
    }
    void terminatePointerSource(const InputSource source)noexcept{
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
    [[nodiscard]] CommandSource* findCommandSource(const InputSource& source)noexcept{
        const auto found = FindRouterCommandSourceIndex(m_commandSources, source);
        return found ? &m_commandSources[*found] : nullptr;
    }
    [[nodiscard]] const CommandSource* findCommandSource(const InputSource& source)const noexcept{
        const auto found = FindRouterCommandSourceIndex(m_commandSources, source);
        return found ? &m_commandSources[*found] : nullptr;
    }
    [[nodiscard]] b8 currentCommandFocus(const CommandSource& source)const noexcept{
        return IsCurrentRouterCommandFocus(m_targets, m_lookup, m_popups, m_windowFocused, m_focus, m_focusDeclaration, source);
    }
    void releaseCommand(const InputEvent& event, const CommandSource& source, InputRoutingResult& result){
        ReleaseRouterCommand(
            m_targets,
            m_lookup,
            m_popups,
            m_windowFocused,
            m_focus,
            m_focusDeclaration,
            m_layoutGeneration,
            event,
            source,
            m_actions,
            m_controlActions,
            m_contextMenuActions,
            m_nextActionSequence,
            result
        );
    }
    void cancelCommandPresses()noexcept{
        CancelRouterCommandPresses(m_commandSources);
    }
    void retireCommandFocus()noexcept{
        RetireRouterCommandFocus(m_commandSources);
    }
    void reconcileCommandPresses()noexcept{
        ReconcileRouterCommandPresses(m_targets, m_lookup, m_popups, m_windowFocused, m_focus, m_focusDeclaration, m_commandSources);
    }
    void cancelPointerPress()noexcept{
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
    [[nodiscard]] const HitTarget* findHitTarget(const Point& position)const noexcept{
        const auto hitIndex = FindRouterHitTargetIndex(m_targets, m_lookup, m_popups, position);
        return hitIndex ? &m_targets[*hitIndex] : nullptr;
    }
    [[nodiscard]] b8 isInteractive(const HitTarget& target)const noexcept{
        return IsRouterTargetInteractive(m_targets, m_lookup, m_popups, target);
    }
    [[nodiscard]] b8 validControlTargets()const noexcept{
        return ValidateStagedHitTargetControlTargets(m_stagedTargets, m_stagedLookup);
    }
    [[nodiscard]] b8 validCommandOwners()const noexcept{
        return ValidateStagedHitTargetCommandOwners(m_stagedTargets, m_stagedLookup);
    }
    [[nodiscard]] const HitTarget* commandHost(const HitTarget& source)const noexcept{
        const auto hostIndex = TryResolveRouterCommandHostIndex(m_targets, m_lookup, m_popups, source);
        return hostIndex ? &m_targets[*hostIndex] : nullptr;
    }
    [[nodiscard]] b8 currentControlCommandOwner(const CommandSource& held)const noexcept{
        return IsCurrentRouterControlCommandOwner(m_targets, m_lookup, m_popups, held);
    }
    [[nodiscard]] const HitTarget* controlHost(const HitTarget& target)const noexcept{
        if(!target.owner.valid())
            return IsValidRouterControlHostTarget(m_popups, target, target) ? &target : nullptr;
        const auto hostIndex = TryResolveRouterControlHostIndex(m_targets, m_lookup, m_popups, target);
        return hostIndex ? &m_targets[*hostIndex] : nullptr;
    }
    [[nodiscard]] b8 currentControlAction(const ControlAction& action)const noexcept{
        return IsCurrentRouterControlAction(m_targets, m_lookup, m_popups, action);
    }
    void reconcileControlActions()noexcept{
        ReconcileRouterControlActions(m_targets, m_lookup, m_popups, m_controlActions, m_commandSources);
    }
    [[nodiscard]] b8 currentContextMenuOwner(const TargetOwner& owner)const noexcept{
        return IsCurrentRouterContextMenuOwner(m_targets, m_lookup, m_popups, m_windowFocused, owner);
    }
    void reconcileContextMenus()noexcept{
        ReconcileRouterContextMenus(m_targets, m_lookup, m_popups, m_windowFocused, m_contextMenuActions, m_secondaryOwner, m_commandSources);
    }
    void appendContextMenu(const HitTarget& target, const Point& position, const InputActionSource::Enum origin, const InputSource& inputSource, InputRoutingResult& result){
        AppendRouterContextMenu(target, m_layoutGeneration, position, origin, inputSource, m_contextMenuActions, m_nextActionSequence, result);
    }
    [[nodiscard]] b8 routeContextMenuCommand(const InputEvent& event, const HitTarget* focused, const b8 alreadyPressed, CommandSource& source)noexcept{
        return focused != nullptr && m_windowFocused && isInteractive(*focused) && TryRouteRouterContextMenuCommand(event, *focused, alreadyPressed, source);
    }
    void routeSecondary(const InputEvent& event, const HitTarget* const hit, InputRoutingResult& result){
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
    void appendControlAction(const HitTarget& host, const HitTarget& source, const ControlActionKind::Enum kind, const f64 delta, const f64 deltaX, InputRoutingResult& result){
        AppendRouterControlAction(host, source, m_layoutGeneration, kind, delta, deltaX, m_controlActions, m_nextActionSequence, result);
    }
    void appendControlAction(const HitTarget& host, const HitTarget& source, const ControlActionKind::Enum kind, const f64 delta, InputRoutingResult& result){
        appendControlAction(host, source, kind, delta, 0.0, result);
    }
    [[nodiscard]] b8 routeControlNavigation(const InputEvent& event, const HitTarget& host, const HitTarget& source, const b8 alreadyPressed, CommandSource& held, InputRoutingResult& result){
        return TryRouteRouterControlNavigation(event, host, source, alreadyPressed, m_layoutGeneration, held, m_controlActions, m_nextActionSequence, result);
    }
    void routeWheel(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
        RouteRouterWheel(
            m_targets,
            m_lookup,
            m_popups,
            event,
            hit != nullptr,
            hit != nullptr ? *hit : HitTarget{},
            m_primaryDown,
            m_pointerSequenceConsumed,
            m_secondaryDown,
            m_secondarySequenceConsumed,
            m_layoutGeneration,
            m_controlActions,
            m_nextActionSequence,
            result
        );
    }
    [[nodiscard]] b8 allowedByPopup(const HitTarget& target)const noexcept{
        return IsTargetAllowedByRouterPopup(m_popups, target);
    }
    [[nodiscard]] b8 popupDescendant(const PopupToken& token, const PopupToken& ancestor)const noexcept{
        return IsPopupTokenDescendant(m_popups, token, ancestor);
    }
    [[nodiscard]] b8 stagePopups(const Span<const PopupScope> scopes){
        return TryStageRouterPopups(scopes, m_popups, m_stagedPopups);
    }
    [[nodiscard]] b8 validPopupTarget(const HitTarget& target)const noexcept{
        return ValidStagedPopupTarget(target, m_stagedPopups);
    }
    void installPopups(const u64 expectedFocusLossGeneration){
        InstallRouterPopups(
            m_targets,
            m_lookup,
            m_windowFocused,
            m_focusLossGeneration,
            expectedFocusLossGeneration,
            m_popupDismissals,
            m_stagedPopups,
            m_popups,
            m_commandSources,
            m_focus,
            m_focusDeclaration,
            m_focusControl
        );
    }
    void retirePopup(const WidgetId id){
        b8 top = false;
        PopupRecord restore;
        if(!TryRetireRouterPopupSubtree(id, m_popups, m_targets, m_popupDismissals, top, restore))
            return;
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
    void cancelPopupFocus(){
        CancelRouterPopupFocus(m_popups, m_popupDismissals);
    }
    void rebuildLookup(){
        m_lookup.clear();
        for(usize index = 0u; index < m_targets.size(); ++index)
            m_lookup.push_back(TargetLookup{ m_targets[index].id.value, static_cast<u32>(index) });
        Sort(m_lookup.begin(), m_lookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs)noexcept{
            return lhs.value < rhs.value;
        });
    }
    void reconcileTargets()noexcept{
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
    void updateHover()noexcept{ setHoverTarget(m_pointerKnown ? findHitTarget(m_pointer) : nullptr); }
    void setHoverTarget(const HitTarget* const hovered)noexcept{
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
    void advanceHoverActivity()noexcept{
        if(m_hoverActivityGeneration == Limit<u64>::s_Max)
            TerminateInvariant();
        ++m_hoverActivityGeneration;
    }
    void clearHover()noexcept{
        m_hover = {};
        m_hoverIdentity = {};
    }
    void cancelInteraction()noexcept{
        advanceHoverActivity();
        CancelRouterInteraction(
            m_actions,
            m_controlActions,
            m_contextMenuActions,
            m_pointerGestures,
            m_commandSources,
            m_activeGestureSequence,
            m_hover,
            m_hoverIdentity,
            m_focus,
            m_capture,
            m_capturePopup,
            m_captureControl,
            m_focusControl,
            m_focusDeclaration,
            m_captureDeclaration,
            m_pointerKnown,
            m_secondaryOwner,
            m_pointerSubmitArmed
        );
    }
    void appendActivation(const HitTarget& target, const InputActionSource::Enum origin, const InputSource& inputSource, InputRoutingResult& result){
        AppendRouterActivation(target, m_layoutGeneration, origin, inputSource, m_actions, m_nextActionSequence, result);
    }
    void appendPointerGesture(const HitTarget& target, InputRoutingResult& result){
        AppendRouterPointerGesture(
            target,
            m_layoutGeneration,
            m_activePointerSource,
            m_interactionPosition,
            m_pointerGestures,
            m_activeGestureSequence,
            m_nextActionSequence,
            result
        );
    }
    void updatePointerGesture(const Point& position, const b8 completed)noexcept{
        if(!TryUpdateRouterPointerGesture(position, completed, m_pointerGestures, m_activeGestureSequence, m_nextActionSequence))
            TerminateInvariant();
    }
    void reconcilePointerGestures()noexcept{
        ReconcileRouterPointerGestures(m_targets, m_lookup, m_popups, m_pointerGestures, m_activeGestureSequence);
    }
    void routePointer(const InputEvent& event, const HitTarget* const hit, InputRoutingResult& result){
        b8 advanceHover = false;
        b8 dismissedPopup = false;
        const b8 ok = RouteRouterPrimary(
            m_targets,
            m_lookup,
            event,
            hit != nullptr,
            hit != nullptr ? *hit : HitTarget{},
            m_secondaryDown,
            m_secondarySequenceConsumed,
            m_layoutGeneration,
            m_activePointerSource,
            m_interactionPosition,
            m_popups,
            m_popupDismissals,
            m_commandSources,
            m_actions,
            m_controlActions,
            m_pointerGestures,
            m_activeGestureSequence,
            m_nextActionSequence,
            m_primaryDown,
            m_pointerSubmitArmed,
            m_pointerSequenceConsumed,
            m_focus,
            m_focusDeclaration,
            m_focusControl,
            m_capture,
            m_capturePopup,
            m_captureDeclaration,
            m_captureControl,
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
        if(!ok)
            TerminateInvariant();
    }
    void routeCommand(const InputEvent& event, InputRoutingResult& result){
        b8 dismissedPopup = false;
        RouteRouterCommand(
            m_targets,
            m_lookup,
            m_windowFocused,
            m_layoutGeneration,
            event,
            m_pointerSequenceConsumed,
            m_secondarySequenceConsumed,
            m_pointerSources,
            m_popups,
            m_popupDismissals,
            m_commandSources,
            m_actions,
            m_controlActions,
            m_contextMenuActions,
            m_pointerGestures,
            m_activeGestureSequence,
            m_nextActionSequence,
            m_pointerSubmitArmed,
            m_secondaryOwner,
            m_hover,
            m_focus,
            m_focusDeclaration,
            m_focusControl,
            m_capture,
            m_capturePopup,
            m_captureDeclaration,
            m_captureControl,
            result,
            dismissedPopup
        );
        if(dismissedPopup){
            reconcileTargets();
            updateHover();
        }
    }
    void assignFocus(const HitTarget& target)noexcept{
        AssignRouterFocus(target, m_commandSources, m_focus, m_focusDeclaration, m_focusControl);
    }
    [[nodiscard]] b8 moveFocus(const b8 reverse)noexcept{
        return TryMoveRouterFocus(m_targets, m_lookup, m_popups, reverse, m_commandSources, m_focus, m_focusDeclaration, m_focusControl);
    }
    [[nodiscard]] b8 moveFocusForTraversal(const b8 reverse){
        b8 dismissedPopup = false;
        const b8 moved = TryMoveRouterFocusForTraversal(
            m_targets,
            m_lookup,
            reverse,
            m_popups,
            m_popupDismissals,
            m_commandSources,
            m_focus,
            m_focusDeclaration,
            m_focusControl,
            dismissedPopup
        );
        if(dismissedPopup)
            reconcileTargets();
        return moved;
    }


private:
    InputBindings m_bindings;
    InputVector<BoundSource> m_boundSources;
    InputVector<CommandSource> m_commandSources;
    InputVector<PointerBinding> m_pointerBindings;
    InputVector<PointerSource> m_pointerSources;
    InputVector<HitTarget> m_targets;
    InputVector<HitTarget> m_stagedTargets;
    InputVector<TargetLookup> m_lookup;
    InputVector<TargetLookup> m_stagedLookup;
    InputVector<InputEvent> m_events;
    InputVector<InputAction> m_actions;
    InputVector<ControlAction> m_controlActions;
    InputVector<ContextMenuAction> m_contextMenuActions;
    InputVector<PointerGestureRecord> m_pointerGestures;
    InputVector<PopupRecord> m_popups;
    InputVector<PopupRecord> m_stagedPopups;
    InputVector<PopupDismissal> m_popupDismissals;
    u64 m_layoutGeneration = 0u;
    u64 m_focusLossGeneration = 0u;
    u64 m_hoverActivityGeneration = 1u;
    u64 m_nextActionSequence = 1u;
    u64 m_activeGestureSequence = 0u;
    WidgetId m_hover;
    HoverIdentity m_hoverIdentity;
    WidgetId m_focus;
    WidgetId m_capture;
    PopupToken m_capturePopup;
    ControlToken m_captureControl;
    ControlToken m_focusControl;
    u64 m_focusDeclaration = 0u;
    u64 m_captureDeclaration = 0u;
    TargetOwner m_secondaryOwner;
    Point m_pointer;
    Point m_interactionPosition;
    Point m_secondaryPosition;
    InputSource m_hoverPointerSource;
    InputSource m_activePointerSource;
    b8 m_pointerKnown = false;
    b8 m_primaryDown = false;
    b8 m_pointerSequenceConsumed = false;
    b8 m_pointerSubmitArmed = false;
    b8 m_secondaryDown = false;
    b8 m_secondarySequenceConsumed = false;
    b8 m_windowFocused = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

