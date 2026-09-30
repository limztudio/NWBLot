// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A single CPU owner serializes input and layout publication; native capture, IME, and clipboard remain OS services.
class InputRouter final : NoCopy{
private:
    struct PointerGestureRecord{
        PointerGesture gesture;
        bool pendingUpdate = true;
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
        bool closing = false;
    };

    struct ControlKeyOwner{
        WidgetId host;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
        WidgetId source;
        u64 sourceDeclarationGeneration = 0u;
        ControlToken sourceControl;
        bool horizontal = false;
    };

    struct ContextMenuOwner{
        WidgetId target;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
    };

    struct HoverIdentity{
        WidgetId owner;
        u64 declarationGeneration = 0u;
        PopupToken popup;
        ControlToken control;
    };


public:
    explicit InputRouter(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] bool queue(const InputEvent& event);
    // Drain events exactly once against the current committed layout; pending actions survive subsequent process calls.
    [[nodiscard]] InputRoutingResult process();
    // Failure preserves the committed layout and all interaction state; generations increase until reset.
    // Context passes the focus-loss generation captured at preparation to fence delayed popup acceptance.
    [[nodiscard]] bool commitTargets(
        const HitTarget* targets, usize count, u64 layoutGeneration,
        const PopupScope* popups = nullptr, usize popupCount = 0u, u64 expectedFocusLossGeneration = Limit<u64>::s_Max
    );
    [[nodiscard]] bool consumePopupDismissal(const PopupToken& token, PopupDismissReason::Enum& reason);
    [[nodiscard]] bool dismissPopup(PopupDismissReason::Enum reason);
    void closePopup(const PopupToken& token);
    void fencePopup(const PopupToken& token);
    [[nodiscard]] bool hasPopup()const{ return !m_popups.empty(); }
    [[nodiscard]] usize popupCount()const{ return m_popups.size(); }
    // Accepted observations are borrowed only until layout publication, reset, or declaration retirement.
    [[nodiscard]] const PopupScope* popupScope(const PopupToken& token)const;
    [[nodiscard]] PopupToken topPopupToken()const{ return m_popups.empty() ? PopupToken{} : m_popups.back().scope.token; }
    [[nodiscard]] u64 focusLossGeneration()const{ return m_focusLossGeneration; }
    // Remove a hidden/deleted declaration immediately while a prior GPU frame remains pending.
    void invalidateTarget(WidgetId id);
    // Transfer keyboard focus to another input owner without cancelling already accepted UI actions.
    void clearFocus();
    // Invalidate interaction and layout after resize/device/root changes; action sequences never restart.
    void reset();
    [[nodiscard]] bool consumeActivation(WidgetId id);
    [[nodiscard]] bool consumeContextMenu(WidgetId id, u64 declarationGeneration, ContextMenuAction& action);
    [[nodiscard]] bool consumeControlAction(
        WidgetId host, u64 declarationGeneration, const ControlToken& token, ControlAction& action
    );
    // A replaced borrowed model cannot receive input from its still displayed earlier lifetime.
    void fenceControl(WidgetId host, u64 declarationGeneration, const ControlToken& token);
    // Active updates are consumed once while retaining their press baseline; terminal updates survive until consumed.
    [[nodiscard]] bool consumePointerGesture(WidgetId id, u64 declarationGeneration, PointerGesture& gesture);
    [[nodiscard]] WidgetId hitTest(const Point& position)const;
    [[nodiscard]] bool wouldConsumePointer(const Point& position)const;
    [[nodiscard]] u64 layoutGeneration()const{ return m_layoutGeneration; }
    [[nodiscard]] const InputVector<HitTarget>& targets()const{ return m_targets; }
    [[nodiscard]] const InputVector<InputAction>& actions()const{ return m_actions; }
    [[nodiscard]] const InputVector<ControlAction>& controlActions()const{ return m_controlActions; }
    [[nodiscard]] WidgetId hover()const{ return m_hover; }
    [[nodiscard]] u64 hoverActivityGeneration()const{ return m_hoverActivityGeneration; }
    [[nodiscard]] WidgetId focus()const{ return m_focus; }
    [[nodiscard]] WidgetId capture()const{ return m_capture; }
    [[nodiscard]] bool primaryDown()const{ return m_primaryDown; }
    [[nodiscard]] bool secondaryDown()const{ return m_secondaryDown; }
    [[nodiscard]] bool pointerKnown()const{ return m_pointerKnown; }
    [[nodiscard]] const Point& pointerPosition()const{ return m_pointer; }
    [[nodiscard]] bool windowFocused()const{ return m_windowFocused; }
    [[nodiscard]] bool wantsPointer()const{
        if(m_primaryDown)
            return m_pointerSequenceConsumed;
        if(m_secondaryDown)
            return m_secondarySequenceConsumed;
        return hasPopup() || m_hover.valid();
    }
    [[nodiscard]] bool wantsKeyboard()const{ return hasPopup() || m_focus.valid() || m_consumedKeys != 0u; }
    [[nodiscard]] bool ownsKey(InputKey::Enum key)const{
        if(key == InputKey::None || key > InputKey::F10)
            return false;
        return (m_consumedKeys & (1u << (static_cast<u8>(key) - 1u))) != 0u;
    }


private:
    [[nodiscard]] const HitTarget* findTarget(WidgetId id, u64 declarationGeneration = 0u)const;
    [[nodiscard]] const HitTarget* findHitTarget(const Point& position)const;
    [[nodiscard]] bool isInteractive(const HitTarget& target)const;
    [[nodiscard]] bool validControlTargets()const;
    [[nodiscard]] bool validKeyboardOwners()const;
    [[nodiscard]] const HitTarget* keyboardHost(const HitTarget& source)const;
    [[nodiscard]] bool currentControlKeyOwner(const ControlKeyOwner& owner)const;
    [[nodiscard]] const HitTarget* controlHost(const HitTarget& target)const;
    [[nodiscard]] bool currentControlAction(const ControlAction& action)const;
    void reconcileControlActions();
    [[nodiscard]] bool currentContextMenuOwner(const ContextMenuOwner& owner)const;
    void reconcileContextMenus();
    void appendContextMenu(const HitTarget& target, const Point& position, bool keyboard, InputRoutingResult& result);
    [[nodiscard]] bool routeContextMenuKey(
        const InputEvent& event, const HitTarget* focused, bool alreadyPressed, InputRoutingResult& result
    );
    void routeSecondary(const InputEvent& event, InputRoutingResult& result);
    void appendControlAction(
        const HitTarget& host, const HitTarget& source, ControlActionKind::Enum kind, f64 delta,
        InputRoutingResult& result, f64 deltaX = 0.0
    );
    [[nodiscard]] bool routeControlKey(
        const InputEvent& event, const HitTarget& host, const HitTarget& source, bool alreadyPressed, InputRoutingResult& result
    );
    void routeWheel(const InputEvent& event, InputRoutingResult& result);
    [[nodiscard]] bool allowedByPopup(const HitTarget& target)const;
    [[nodiscard]] bool popupDescendant(const PopupToken& token, const PopupToken& ancestor)const;
    [[nodiscard]] bool stagePopups(const PopupScope* scopes, usize count);
    [[nodiscard]] bool validPopupTarget(const HitTarget& target)const;
    void installPopups(u64 expectedFocusLossGeneration);
    void retirePopup(WidgetId id);
    void cancelPopupFocus();
    void rebuildLookup();
    void reconcileTargets();
    void updateHover();
    void advanceHoverActivity();
    void clearHover();
    void cancelPointerCapture();
    void cancelInteraction();
    void appendActivation(const HitTarget& target, InputActionSource::Enum source, InputRoutingResult& result);
    void appendPointerGesture(const HitTarget& target, InputRoutingResult& result);
    void updatePointerGesture(const Point& position, bool completed);
    void reconcilePointerGestures();
    void routePointer(const InputEvent& event, InputRoutingResult& result);
    void routeKeyboard(const InputEvent& event, InputRoutingResult& result);
    [[nodiscard]] bool moveFocus(bool reverse);
    [[nodiscard]] bool moveFocusOnTab(bool reverse);


private:
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
    Point m_pointer;
    bool m_pointerKnown = false;
    bool m_primaryDown = false;
    bool m_pointerSequenceConsumed = false;
    bool m_secondaryDown = false;
    bool m_secondarySequenceConsumed = false;
    ContextMenuOwner m_secondaryOwner;
    bool m_windowFocused = true;
    u32 m_pressedKeys = 0u;
    u32 m_consumedKeys = 0u;
    Array<ControlKeyOwner, 32u> m_controlKeyOwners{};
    Array<ContextMenuOwner, 2u> m_contextMenuKeyOwners{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

