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

template<typename PopupsT, typename IsRootF>
[[nodiscard]] inline b8 MarkRemovedPopupSubtree(const PopupsT& popups, IsRootF&& isRoot, Array<b8, s_InputMaxPopups>& outRemoved)noexcept{
    outRemoved = {};
    b8 found = false;
    for(usize index = 0u; index < popups.size(); ++index){
        outRemoved[index] = isRoot(popups[index].scope.token);
        for(usize ancestor = 0u; ancestor < index && !outRemoved[index]; ++ancestor)
            outRemoved[index] = outRemoved[ancestor] && popups[index].scope.parent == popups[ancestor].scope.token;
        found |= outRemoved[index];
    }
    return found;
}

template<typename PopupsT>
[[nodiscard]] inline b8 IsRemovedPopupToken(const PopupsT& popups, const Array<b8, s_InputMaxPopups>& removed, const PopupToken& token)noexcept{
    for(usize popup = 0u; popup < popups.size(); ++popup){
        if(removed[popup] && token == popups[popup].scope.token)
            return true;
    }
    return false;
}

template<typename ItemsT, typename PopupsT, typename TokenOfF>
inline void EraseIfRemovedPopupToken(ItemsT& items, const PopupsT& popups, const Array<b8, s_InputMaxPopups>& removed, TokenOfF&& tokenOf)noexcept{
    for(usize index = items.size(); index > 0u; --index){
        if(IsRemovedPopupToken(popups, removed, tokenOf(items[index - 1u])))
            items.erase(items.begin() + static_cast<isize>(index - 1u));
    }
}

template<typename PopupsT>
inline void EraseRemovedPopups(PopupsT& popups, const Array<b8, s_InputMaxPopups>& removed)noexcept{
    for(usize index = popups.size(); index > 0u; --index){
        if(removed[index - 1u])
            popups.erase(popups.begin() + static_cast<isize>(index - 1u));
    }
}


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
    [[nodiscard]] Expected<PopupDismissReason::Enum> consumePopupDismissal(const PopupToken& token)noexcept;
    [[nodiscard]] b8 dismissPopup(PopupDismissReason::Enum reason);
    void closePopup(const PopupToken& token)noexcept;
    void fencePopup(const PopupToken& token)noexcept;

    [[nodiscard]] b8 hasPopup()const noexcept{ return !m_popups.empty(); }

    [[nodiscard]] usize popupCount()const noexcept{ return m_popups.size(); }

    // Accepted observations are borrowed only until layout publication, reset, or declaration retirement.
    [[nodiscard]] const PopupScope* popupScope(const PopupToken& token)const noexcept;
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
    void cancelDeviceInput(u64 device)noexcept;
    [[nodiscard]] b8 consumeActivation(WidgetId id)noexcept;
    [[nodiscard]] Expected<ContextMenuAction> consumeContextMenu(const WidgetId id, const u64 declarationGeneration)noexcept{
        const HitTarget* target = findTarget(id, declarationGeneration);
        if(declarationGeneration == 0u || target == nullptr || !target->contextMenu || !m_windowFocused || !isInteractive(*target))
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < m_contextMenuActions.size(); ++index){
            const auto& candidate = m_contextMenuActions[index];
            if(candidate.id.target != id || candidate.id.declarationGeneration != declarationGeneration || candidate.popup != target->popup || candidate.control != target->control)
                continue;
            const ContextMenuAction action = candidate;
            m_contextMenuActions.erase(m_contextMenuActions.begin() + static_cast<isize>(index));
            return action;
        }
        return MakeUnexpected(Failure{});
    }
    [[nodiscard]] Expected<ControlAction> consumeControlAction(WidgetId host, u64 declarationGeneration, const ControlToken& token)noexcept;
    // A replaced borrowed model cannot receive input from its still displayed earlier lifetime.
    void fenceControl(WidgetId host, u64 declarationGeneration, const ControlToken& token)noexcept;
    // Active updates are consumed once while retaining their press baseline; terminal updates survive until consumed.
    [[nodiscard]] Expected<PointerGesture> consumePointerGesture(const WidgetId id, const u64 declarationGeneration)noexcept{
        const HitTarget* target = findTarget(id, declarationGeneration);
        if(declarationGeneration == 0u || target == nullptr || !isInteractive(*target) || !target->pointerGesture)
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < m_pointerGestures.size(); ++index){
            auto& record = m_pointerGestures[index];
            if(record.gesture.id.target != id || record.gesture.id.declarationGeneration != declarationGeneration || !record.pendingUpdate || record.gesture.popup != target->popup || record.gesture.control != target->control)
                continue;
            const PointerGesture gesture = record.gesture;
            if(record.gesture.state == PointerGestureState::Completed)
                m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
            else
                record.pendingUpdate = false;
            return gesture;
        }
        return MakeUnexpected(Failure{});
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

    [[nodiscard]] b8 acceptsPointerSource(const InputSource& source)const noexcept;
    [[nodiscard]] b8 ownsPointerSource(const InputSource& source, PointerIntent::Enum intent = PointerIntent::Activate)const noexcept;

    [[nodiscard]] b8 primaryDown()const noexcept{ return m_primaryDown; }

    [[nodiscard]] b8 secondaryDown()const noexcept{ return m_secondaryDown; }

    [[nodiscard]] b8 pointerKnown()const noexcept{ return m_pointerKnown; }

    // Only an explicitly hover-capable source supplies cursor geometry; contact gestures keep their own position.
    [[nodiscard]] const Point& pointerPosition()const noexcept{ return m_pointer; }

    [[nodiscard]] b8 windowFocused()const noexcept{ return m_windowFocused; }

    [[nodiscard]] b8 wantsPointer()const noexcept;
    [[nodiscard]] b8 wantsCommands()const noexcept{
        return hasPopup() || m_focus.valid() || FindIf(m_commandSources.begin(), m_commandSources.end(), [](const CommandSource& source)noexcept{ return source.consumed; }) != m_commandSources.end();
    }
    [[nodiscard]] b8 ownsKey(const i32 key)const noexcept{
        return InputBindings::ValidKey(key) && ownsSource({ 0u, static_cast<u64>(key) + 1u });
    }
    [[nodiscard]] b8 ownsSource(const InputSource& source)const noexcept;
    // Held commands cannot switch editors or begin editing after their delegated owner has retired.
    [[nodiscard]] b8 canEditCommand(const InputEvent& event)const noexcept;
    // Query a queued release before process retires its physical source.
    [[nodiscard]] b8 canSubmitCommand(const InputEvent& event)const noexcept;
    void cancelSubmitCommands(WidgetId id)noexcept;
    [[nodiscard]] b8 pressed(WidgetId id)const noexcept;


private:
    [[nodiscard]] Expected<InputEvent> resolveSourceEvent(const InputEvent& event);
    [[nodiscard]] Expected<InputEvent> resolvePointerEvent(const InputEvent& event);
    void retireQueuedReleases()noexcept;
    [[nodiscard]] PointerSource* findPointerSource(const InputSource& source, PointerIntent::Enum intent)noexcept;
    [[nodiscard]] const PointerSource* findPointerSource(const InputSource& source, PointerIntent::Enum intent)const noexcept;
    void routePointerSource(const InputEvent& event, InputRoutingResult& result);
    void observePointer(const InputEvent& event, const HitTarget* hit)noexcept;
    void terminatePointerSource(InputSource source)noexcept;
    [[nodiscard]] CommandSource* findCommandSource(const InputSource& source)noexcept;
    [[nodiscard]] const CommandSource* findCommandSource(const InputSource& source)const noexcept;
    [[nodiscard]] b8 currentCommandFocus(const CommandSource& source)const noexcept;
    void releaseCommand(const InputEvent& event, const CommandSource& source, InputRoutingResult& result);
    void cancelCommandPresses()noexcept;
    void retireCommandFocus()noexcept;
    void reconcileCommandPresses()noexcept;
    void cancelPointerPress()noexcept;
    [[nodiscard]] const HitTarget* findHitTarget(const Point& position)const noexcept;
    [[nodiscard]] b8 isInteractive(const HitTarget& target)const noexcept{
        return target.enabled && HasVisibleBounds(target) && allowedByPopup(target) && (!target.owner.valid() || controlHost(target) != nullptr);
    }
    [[nodiscard]] b8 validControlTargets()const noexcept;
    [[nodiscard]] b8 validCommandOwners()const noexcept;
    [[nodiscard]] const HitTarget* commandHost(const HitTarget& source)const noexcept;
    [[nodiscard]] b8 currentControlCommandOwner(const CommandSource& held)const noexcept;
    [[nodiscard]] const HitTarget* controlHost(const HitTarget& target)const noexcept;
    [[nodiscard]] b8 currentControlAction(const ControlAction& action)const noexcept;
    void reconcileControlActions()noexcept;
    [[nodiscard]] b8 currentContextMenuOwner(const TargetOwner& owner)const noexcept;
    void reconcileContextMenus()noexcept{
        for(usize index = 0u; index < m_contextMenuActions.size();){
            const auto& action = m_contextMenuActions[index];
            if(!currentContextMenuOwner({ action.id.target, action.id.declarationGeneration, action.popup, action.control }))
                m_contextMenuActions.erase(m_contextMenuActions.begin() + static_cast<isize>(index));
            else
                ++index;
        }
        if(!currentContextMenuOwner(m_secondaryOwner))
            m_secondaryOwner = {};
        for(auto& source : m_commandSources){
            if(source.contextArmed && !currentContextMenuOwner(source.focusOwner))
                source.contextArmed = false;
        }
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
    [[nodiscard]] b8 routeControlNavigation(const InputEvent& event, const HitTarget& host, const HitTarget& source, b8 alreadyPressed, CommandSource& held, InputRoutingResult& result);
    void routeWheel(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    [[nodiscard]] b8 allowedByPopup(const HitTarget& target)const noexcept;
    [[nodiscard]] b8 popupDescendant(const PopupToken& token, const PopupToken& ancestor)const noexcept;
    [[nodiscard]] b8 stagePopups(Span<const PopupScope> scopes);
    [[nodiscard]] b8 validPopupTarget(const HitTarget& target)const noexcept;
    void installPopups(u64 expectedFocusLossGeneration);
    void retirePopup(WidgetId id);
    void cancelPopupFocus();
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
        for(usize index = 0u; index < m_pointerGestures.size();){
            const auto& gesture = m_pointerGestures[index].gesture;
            const HitTarget* target = findTarget(gesture.id.target, gesture.id.declarationGeneration);
            if(target == nullptr || !isInteractive(*target) || !target->pointerGesture || gesture.popup != target->popup || gesture.control != target->control){
                if(gesture.id.sequence == m_activeGestureSequence)
                    m_activeGestureSequence = 0u;
                m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
            }
            else
                ++index;
        }
    }
    void routePointer(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    void routeCommand(const InputEvent& event, InputRoutingResult& result);
    void assignFocus(const HitTarget& target)noexcept;
    [[nodiscard]] b8 moveFocus(b8 reverse)noexcept;
    [[nodiscard]] b8 moveFocusForTraversal(b8 reverse);


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

