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


public:
    explicit InputRouter(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] bool queue(const InputEvent& event);
    // Drain events exactly once against the current committed layout; pending actions survive subsequent process calls.
    [[nodiscard]] InputRoutingResult process();
    // Failure preserves the committed layout and all interaction state; generations increase until reset.
    [[nodiscard]] bool commitTargets(const HitTarget* targets, usize count, u64 layoutGeneration);
    // Remove a hidden/deleted declaration immediately while a prior GPU frame remains pending.
    void invalidateTarget(WidgetId id);
    // Transfer keyboard focus to another input owner without cancelling already accepted UI actions.
    void clearFocus();
    // Invalidate interaction and layout after resize/device/root changes; action sequences never restart.
    void reset();
    [[nodiscard]] bool consumeActivation(WidgetId id);
    // Active updates are consumed once while retaining their press baseline; terminal updates survive until consumed.
    [[nodiscard]] bool consumePointerGesture(WidgetId id, u64 declarationGeneration, PointerGesture& gesture);
    [[nodiscard]] WidgetId hitTest(const Point& position)const;
    [[nodiscard]] bool wouldConsumePointer(const Point& position)const;
    [[nodiscard]] u64 layoutGeneration()const{ return m_layoutGeneration; }
    [[nodiscard]] const InputVector<HitTarget>& targets()const{ return m_targets; }
    [[nodiscard]] const InputVector<InputAction>& actions()const{ return m_actions; }
    [[nodiscard]] WidgetId hover()const{ return m_hover; }
    [[nodiscard]] WidgetId focus()const{ return m_focus; }
    [[nodiscard]] WidgetId capture()const{ return m_capture; }
    [[nodiscard]] bool primaryDown()const{ return m_primaryDown; }
    [[nodiscard]] bool wantsPointer()const{ return m_primaryDown ? m_pointerSequenceConsumed : m_hover.valid(); }
    [[nodiscard]] bool wantsKeyboard()const{ return m_focus.valid() || m_consumedKeys != 0u; }
    [[nodiscard]] bool ownsKey(InputKey::Enum key)const{
        if(key == InputKey::None || key > InputKey::Escape)
            return false;
        return (m_consumedKeys & static_cast<u8>(1u << (static_cast<u8>(key) - 1u))) != 0u;
    }


private:
    [[nodiscard]] const HitTarget* findTarget(WidgetId id, u64 declarationGeneration = 0u)const;
    [[nodiscard]] const HitTarget* findHitTarget(const Point& position)const;
    [[nodiscard]] bool isInteractive(const HitTarget& target)const;
    void reconcileTargets();
    void updateHover();
    void cancelPointerCapture();
    void cancelInteraction();
    void appendActivation(const HitTarget& target, InputActionSource::Enum source, InputRoutingResult& result);
    void appendPointerGesture(const HitTarget& target, InputRoutingResult& result);
    void updatePointerGesture(const Point& position, bool completed);
    void reconcilePointerGestures();
    void routePointer(const InputEvent& event, InputRoutingResult& result);
    void routeKeyboard(const InputEvent& event, InputRoutingResult& result);
    [[nodiscard]] bool moveFocus(bool reverse);


private:
    InputVector<HitTarget> m_targets;
    InputVector<HitTarget> m_stagedTargets;
    InputVector<TargetLookup> m_lookup;
    InputVector<TargetLookup> m_stagedLookup;
    InputVector<InputEvent> m_events;
    InputVector<InputAction> m_actions;
    InputVector<PointerGestureRecord> m_pointerGestures;
    u64 m_layoutGeneration = 0u;
    u64 m_nextActionSequence = 1u;
    u64 m_activeGestureSequence = 0u;
    WidgetId m_hover;
    WidgetId m_focus;
    WidgetId m_capture;
    u64 m_focusDeclaration = 0u;
    u64 m_captureDeclaration = 0u;
    Point m_pointer;
    bool m_pointerKnown = false;
    bool m_primaryDown = false;
    bool m_pointerSequenceConsumed = false;
    u8 m_pressedKeys = 0u;
    u8 m_consumedKeys = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

