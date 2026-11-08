// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputTargetTable;
class InputPopupState;
class InputCommandState;


struct InputPointerRouteEffects{
    PopupDismissRequest popupDismissal;
    b8 advanceHover = false;
    b8 dismissedPopup = false;
    b8 ok = true;
};


class InputPointerState final : NoCopy{
public:
    explicit InputPointerState(Core::Alloc::GlobalArena& arena);


public:
    void reserveSources();
    void reserveGestures();
    [[nodiscard]] b8 accepts(const InputSource& source)const noexcept;
    [[nodiscard]] b8 owns(const InputSource& source, PointerIntent::Enum intent)const noexcept;
    [[nodiscard]] b8 wants(b8 hasPopup, WidgetId hover)const noexcept;
    [[nodiscard]] InputPointerRouteEffects routePrimary(const InputEvent& event, const HitTarget* hit, InputTargetTable& targets, InputPopupState& popups, InputCommandState& commands, Vector<InputAction, Core::Alloc::GlobalArena>& actions, Vector<ControlAction, Core::Alloc::GlobalArena>& controlActions, u64& nextActionSequence, InputRoutingResult& result);
    [[nodiscard]] InputPointerRouteEffects routeSecondary(const InputEvent& event, const HitTarget* hit, InputTargetTable& targets, InputPopupState& popups, b8 windowFocused, Vector<ContextMenuAction, Core::Alloc::GlobalArena>& contextMenuActions, u64& nextActionSequence, InputRoutingResult& result);
    void routeWheel(const InputEvent& event, const HitTarget* hit, InputTargetTable& targets, InputPopupState& popups, Vector<ControlAction, Core::Alloc::GlobalArena>& controlActions, u64& nextActionSequence, InputRoutingResult& result);
    [[nodiscard]] Vector<PointerSource, Core::Alloc::GlobalArena>& sources()noexcept{ return m_pointerSources; }
    [[nodiscard]] const Vector<PointerSource, Core::Alloc::GlobalArena>& sources()const noexcept{ return m_pointerSources; }
    [[nodiscard]] Vector<PointerGestureRecord, Core::Alloc::GlobalArena>& gestures()noexcept{ return m_pointerGestures; }
    [[nodiscard]] const Vector<PointerGestureRecord, Core::Alloc::GlobalArena>& gestures()const noexcept{ return m_pointerGestures; }
    [[nodiscard]] u64& gestureSequence()noexcept{ return m_activeGestureSequence; }
    [[nodiscard]] const u64& gestureSequence()const noexcept{ return m_activeGestureSequence; }
    [[nodiscard]] WidgetId& capture()noexcept{ return m_capture; }
    [[nodiscard]] const WidgetId& capture()const noexcept{ return m_capture; }
    [[nodiscard]] PopupToken& capturePopup()noexcept{ return m_capturePopup; }
    [[nodiscard]] const PopupToken& capturePopup()const noexcept{ return m_capturePopup; }
    [[nodiscard]] ControlToken& captureControl()noexcept{ return m_captureControl; }
    [[nodiscard]] const ControlToken& captureControl()const noexcept{ return m_captureControl; }
    [[nodiscard]] u64& captureDeclaration()noexcept{ return m_captureDeclaration; }
    [[nodiscard]] const u64& captureDeclaration()const noexcept{ return m_captureDeclaration; }
    [[nodiscard]] TargetOwner& secondaryOwner()noexcept{ return m_secondaryOwner; }
    [[nodiscard]] const TargetOwner& secondaryOwner()const noexcept{ return m_secondaryOwner; }
    [[nodiscard]] Point& interactionPosition()noexcept{ return m_interactionPosition; }
    [[nodiscard]] const Point& interactionPosition()const noexcept{ return m_interactionPosition; }
    [[nodiscard]] Point& secondaryPosition()noexcept{ return m_secondaryPosition; }
    [[nodiscard]] const Point& secondaryPosition()const noexcept{ return m_secondaryPosition; }
    [[nodiscard]] InputSource& activeSource()noexcept{ return m_activePointerSource; }
    [[nodiscard]] const InputSource& activeSource()const noexcept{ return m_activePointerSource; }
    [[nodiscard]] b8& primaryDown()noexcept{ return m_primaryDown; }
    [[nodiscard]] const b8& primaryDown()const noexcept{ return m_primaryDown; }
    [[nodiscard]] b8& primaryConsumed()noexcept{ return m_pointerSequenceConsumed; }
    [[nodiscard]] const b8& primaryConsumed()const noexcept{ return m_pointerSequenceConsumed; }
    [[nodiscard]] b8& submitArmed()noexcept{ return m_pointerSubmitArmed; }
    [[nodiscard]] const b8& submitArmed()const noexcept{ return m_pointerSubmitArmed; }
    [[nodiscard]] b8& secondaryDown()noexcept{ return m_secondaryDown; }
    [[nodiscard]] const b8& secondaryDown()const noexcept{ return m_secondaryDown; }
    [[nodiscard]] b8& secondaryConsumed()noexcept{ return m_secondarySequenceConsumed; }
    [[nodiscard]] const b8& secondaryConsumed()const noexcept{ return m_secondarySequenceConsumed; }


private:
    Vector<PointerSource, Core::Alloc::GlobalArena> m_pointerSources;
    Vector<PointerGestureRecord, Core::Alloc::GlobalArena> m_pointerGestures;
    u64 m_activeGestureSequence = 0u;
    WidgetId m_capture;
    PopupToken m_capturePopup;
    ControlToken m_captureControl;
    u64 m_captureDeclaration = 0u;
    TargetOwner m_secondaryOwner;
    Point m_interactionPosition;
    Point m_secondaryPosition;
    InputSource m_activePointerSource;
    b8 m_primaryDown = false;
    b8 m_pointerSequenceConsumed = false;
    b8 m_pointerSubmitArmed = false;
    b8 m_secondaryDown = false;
    b8 m_secondarySequenceConsumed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

