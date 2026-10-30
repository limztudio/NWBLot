// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bindings.h"
#include "../id.h"
#include "../paint.h"

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
    struct PointerGestureRecord{
        PointerGesture gesture;
        b8 pendingUpdate = true;
    };
    struct TargetLookup{
        u64 value = 0u;
        u32 index = 0u;
    };
    struct PopupRecord{
        PopupScope scope;
        WidgetId restoreFocus;
        u64 restoreDeclaration = 0u;
        PopupToken restorePopup;
        b8 closing = false;
    };
    struct ControlCommandOwner{
        WidgetId host;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
        b8 horizontal = false;
    };
    struct TargetOwner{
        WidgetId target;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
    };
    struct BoundSource{
        InputSource source;
        InputCommandIntent intent;
    };
    struct CommandSource{
        InputSource source;
        ControlCommandOwner controlOwner{};
        TargetOwner focusOwner{};
        Point contextPosition;
        b8 consumed = false;
        b8 delegated = false;
        b8 submitArmed = false;
        b8 contextArmed = false;
    };
    struct HoverIdentity{
        WidgetId owner;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
    };
    struct PointerBinding{
        InputSource source;
        PointerIntent::Enum intent = PointerIntent::Activate;
        b8 hoverCapable = false;
    };
    struct PointerSource{
        InputSource source;
        PointerIntent::Enum intent = PointerIntent::Activate;
        b8 consumed = false;
        b8 inert = false;
    };


private:
    [[nodiscard]] static b8 IsPointerEvent(InputEventType::Enum type)noexcept;


public:
    explicit InputRouter(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<InputEvent> queue(const InputEvent& event);
    [[nodiscard]] Expected<InputEvent> queuePointer(const PointerInput& input);
    [[nodiscard]] b8 setBindings(const InputKeyBinding* bindings, usize count);
    void restoreDefaultBindings();

    [[nodiscard]] const InputBindings& bindings()const noexcept{ return m_bindings; }

    // Drain events exactly once against the current committed layout; pending actions survive subsequent process calls.
    [[nodiscard]] InputRoutingResult process();
    // Failure preserves the committed layout and all interaction state; generations increase until reset.
    // Context passes the focus-loss generation captured at preparation to fence delayed popup acceptance.
    [[nodiscard]] b8 commitTargets(const HitTarget* targets, usize count, u64 layoutGeneration, const PopupScope* popups = nullptr, usize popupCount = 0u, u64 expectedFocusLossGeneration = Limit<u64>::s_Max);
    [[nodiscard]] Expected<PopupDismissReason::Enum> consumePopupDismissal(const PopupToken& token)noexcept;
    [[nodiscard]] b8 dismissPopup(PopupDismissReason::Enum reason);
    void closePopup(const PopupToken& token)noexcept;
    void fencePopup(const PopupToken& token)noexcept;

    [[nodiscard]] b8 hasPopup()const noexcept{ return !m_popups.empty(); }

    [[nodiscard]] usize popupCount()const noexcept{ return m_popups.size(); }

    // Accepted observations are borrowed only until layout publication, reset, or declaration retirement.
    [[nodiscard]] const PopupScope* popupScope(const PopupToken& token)const noexcept;
    [[nodiscard]] const HitTarget* findTarget(WidgetId id, u64 declarationGeneration = 0u)const noexcept;

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
    [[nodiscard]] Expected<ContextMenuAction> consumeContextMenu(WidgetId id, u64 declarationGeneration)noexcept;
    [[nodiscard]] Expected<ControlAction> consumeControlAction(WidgetId host, u64 declarationGeneration, const ControlToken& token)noexcept;
    // A replaced borrowed model cannot receive input from its still displayed earlier lifetime.
    void fenceControl(WidgetId host, u64 declarationGeneration, const ControlToken& token)noexcept;
    // Active updates are consumed once while retaining their press baseline; terminal updates survive until consumed.
    [[nodiscard]] Expected<PointerGesture> consumePointerGesture(WidgetId id, u64 declarationGeneration)noexcept;
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
    [[nodiscard]] b8 wantsCommands()const noexcept;
    [[nodiscard]] b8 ownsKey(i32 key)const noexcept;
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
    [[nodiscard]] b8 isInteractive(const HitTarget& target)const noexcept;
    [[nodiscard]] b8 validControlTargets()const noexcept;
    [[nodiscard]] b8 validCommandOwners()const noexcept;
    [[nodiscard]] const HitTarget* commandHost(const HitTarget& source)const noexcept;
    [[nodiscard]] b8 currentControlCommandOwner(const CommandSource& held)const noexcept;
    [[nodiscard]] const HitTarget* controlHost(const HitTarget& target)const noexcept;
    [[nodiscard]] b8 currentControlAction(const ControlAction& action)const noexcept;
    void reconcileControlActions()noexcept;
    [[nodiscard]] b8 currentContextMenuOwner(const TargetOwner& owner)const noexcept;
    void reconcileContextMenus()noexcept;
    void appendContextMenu(const HitTarget& target, const Point& position, InputActionSource::Enum origin, const InputSource& inputSource, InputRoutingResult& result);
    [[nodiscard]] b8 routeContextMenuCommand(const InputEvent& event, const HitTarget* focused, b8 alreadyPressed, CommandSource& source)noexcept;
    void routeSecondary(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    void appendControlAction(InputRoutingResult& result, const HitTarget& host, const HitTarget& source, ControlActionKind::Enum kind, f64 delta, f64 deltaX = 0.0);
    [[nodiscard]] b8 routeControlNavigation(const InputEvent& event, const HitTarget& host, const HitTarget& source, b8 alreadyPressed, CommandSource& held, InputRoutingResult& result);
    void routeWheel(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    [[nodiscard]] b8 allowedByPopup(const HitTarget& target)const noexcept;
    [[nodiscard]] b8 popupDescendant(const PopupToken& token, const PopupToken& ancestor)const noexcept;
    [[nodiscard]] b8 stagePopups(const PopupScope* scopes, usize count);
    [[nodiscard]] b8 validPopupTarget(const HitTarget& target)const noexcept;
    void installPopups(u64 expectedFocusLossGeneration);
    void retirePopup(WidgetId id);
    void cancelPopupFocus();
    void rebuildLookup();
    void reconcileTargets()noexcept;
    void updateHover()noexcept;
    void setHoverTarget(const HitTarget* hovered)noexcept;
    void advanceHoverActivity()noexcept;
    void clearHover()noexcept;
    void cancelInteraction()noexcept;
    void appendActivation(const HitTarget& target, InputActionSource::Enum origin, const InputSource& inputSource, InputRoutingResult& result);
    void appendPointerGesture(const HitTarget& target, InputRoutingResult& result);
    void updatePointerGesture(const Point& position, b8 completed)noexcept;
    void reconcilePointerGestures()noexcept;
    void routePointer(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result);
    void routeCommand(const InputEvent& event, InputRoutingResult& result);
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

