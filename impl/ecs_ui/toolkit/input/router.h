// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bindings.h"

#include <impl/ecs_ui/toolkit/rect_math.h>

#include <impl/ecs_ui/toolkit/id.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using InputVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A single CPU owner serializes input and layout publication; native capture, IME, and clipboard remain OS services.
class InputRouter final : NoCopy{
private:
    [[nodiscard]] static const HitTarget* FindTargetInTable(Span<const HitTarget> targets, Span<const TargetLookup> lookup, WidgetId id, u64 declarationGeneration = 0u)noexcept;
    [[nodiscard]] static b8 HasVisibleBounds(const HitTarget& target)noexcept{
        return HasVisibleUiRectIntersection(target.rectangle, target.clip);
    }


public:
    explicit InputRouter(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<InputEvent> queue(const InputEvent& event);
    [[nodiscard]] Expected<InputEvent> queuePointer(const PointerInput& input);
    [[nodiscard]] b8 setBindings(const Span<const InputKeyBinding> bindings){ return m_bindings.set(bindings); }
    void restoreDefaultBindings(){ m_bindings.restoreDefaults(); }

    [[nodiscard]] const InputBindings& bindings()const noexcept{ return m_bindings; }

    // Drain events exactly once against the current committed layout; pending actions survive subsequent process calls.
    [[nodiscard]] InputRoutingResult process();
    // Failure preserves the committed layout and all interaction state; generations increase until reset.
    // Context passes the focus-loss generation captured at preparation to fence delayed popup acceptance.
    [[nodiscard]] b8 commitTargets(Span<const HitTarget> targets, u64 layoutGeneration, Span<const PopupScope> popups = {}, u64 expectedFocusLossGeneration = Limit<u64>::s_Max);
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
    void invalidateTarget(WidgetId id);
    // Transfer command focus to another input owner without cancelling already accepted UI actions.
    void clearFocus()noexcept;
    // Invalidate interaction and layout after resize/device/root changes; action sequences never restart.
    void reset()noexcept;
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
    [[nodiscard]] WidgetId hitTest(const Point& position)const noexcept;
    [[nodiscard]] b8 wouldConsumePointer(const Point& position)const noexcept;

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
    [[nodiscard]] b8 ownsPointerSource(const InputSource& source, PointerIntent::Enum intent = PointerIntent::Activate)const noexcept;

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
        return hasPopup() || m_focus.valid() || FindIf(m_commandSources.begin(), m_commandSources.end(), [](const CommandSource& source)noexcept{ return source.consumed; }) != m_commandSources.end();
    }
    [[nodiscard]] b8 ownsKey(const i32 key)const noexcept{
        return InputBindings::ValidKey(key) && ownsSource({ 0u, static_cast<u64>(key) + 1u });
    }
    [[nodiscard]] b8 ownsSource(const InputSource& source)const noexcept;
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
    void retireQueuedReleases()noexcept;
    [[nodiscard]] PointerSource* findPointerSource(const InputSource& source, PointerIntent::Enum intent)noexcept;
    [[nodiscard]] const PointerSource* findPointerSource(const InputSource& source, PointerIntent::Enum intent)const noexcept;
    void routePointerSource(const InputEvent& event, InputRoutingResult& result);
    void observePointer(const InputEvent& event, const HitTarget* hit)noexcept;
    void terminatePointerSource(InputSource source)noexcept;
    [[nodiscard]] CommandSource* findCommandSource(const InputSource& source)noexcept;
    [[nodiscard]] const CommandSource* findCommandSource(const InputSource& source)const noexcept;
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
    void cancelPointerPress()noexcept;
    [[nodiscard]] const HitTarget* findHitTarget(const Point& position)const noexcept;
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
    void routeSecondary(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
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
    void rebuildLookup();
    void reconcileTargets()noexcept;
    void updateHover()noexcept{ setHoverTarget(m_pointerKnown ? findHitTarget(m_pointer) : nullptr); }
    void setHoverTarget(const HitTarget* hovered)noexcept;
    void advanceHoverActivity()noexcept;
    void clearHover()noexcept;
    void cancelInteraction()noexcept;
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
    void routePointer(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    void routeCommand(const InputEvent& event, InputRoutingResult& result);
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

