// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputCommandState final : NoCopy{
public:
    explicit InputCommandState(Core::Alloc::GlobalArena& arena);


public:
    void reserve();
    void clearFocus()noexcept;
    [[nodiscard]] b8 wants(Span<const PopupRecord> popups)const noexcept;
    [[nodiscard]] b8 owns(const InputSource& source)const noexcept;
    [[nodiscard]] b8 canEdit(Span<const HitTarget> targets, Span<const TargetLookup> lookup, Span<const PopupRecord> popups, b8 windowFocused, const InputEvent& event)const noexcept;
    [[nodiscard]] b8 canSubmit(Span<const HitTarget> targets, Span<const TargetLookup> lookup, Span<const PopupRecord> popups, b8 windowFocused, const InputEvent& event)const noexcept;
    void cancelSubmit(WidgetId id)noexcept;
    [[nodiscard]] b8 isPressed(Span<const HitTarget> targets, Span<const TargetLookup> lookup, Span<const PopupRecord> popups, b8 windowFocused, WidgetId id)const noexcept;
    [[nodiscard]] Vector<CommandSource, Core::Alloc::GlobalArena>& records()noexcept{ return m_sources; }
    [[nodiscard]] WidgetId& focus()noexcept{ return m_focus; }
    [[nodiscard]] WidgetId focus()const noexcept{ return m_focus; }
    [[nodiscard]] u64& declaration()noexcept{ return m_focusDeclaration; }
    [[nodiscard]] ControlToken& control()noexcept{ return m_focusControl; }


private:
    Vector<CommandSource, Core::Alloc::GlobalArena> m_sources;
    WidgetId m_focus;
    ControlToken m_focusControl;
    u64 m_focusDeclaration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

