// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_state.h"

#include <impl/ecs_ui/toolkit/input/router_command_focus.adhd.h>
#include <impl/ecs_ui/toolkit/input/router_command_sources.adhd.h>
#include <impl/ecs_ui/toolkit/input/router_popup.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputCommandState::InputCommandState(Core::Alloc::GlobalArena& arena)
    : m_sources(arena)
{}

void InputCommandState::reserve(){
    m_sources.reserve(s_InputSourceReserve);
}

void InputCommandState::clearFocus()noexcept{
    RetireRouterCommandFocus(m_sources);
    m_focus = {};
    m_focusDeclaration = 0u;
    m_focusControl = {};
}

b8 InputCommandState::wants(const Span<const PopupRecord> popups)const noexcept{
    return WantsRouterCommands(popups, m_focus, m_sources);
}

b8 InputCommandState::owns(const InputSource& source)const noexcept{
    return OwnsRouterCommandSource(m_sources, source);
}

b8 InputCommandState::canEdit(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const Span<const PopupRecord> popups, const b8 windowFocused, const InputEvent& event)const noexcept{
    return CanRouterEditCommand(targets, lookup, popups, m_sources, windowFocused, m_focus, m_focusDeclaration, event);
}

b8 InputCommandState::canSubmit(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const Span<const PopupRecord> popups, const b8 windowFocused, const InputEvent& event)const noexcept{
    return CanRouterSubmitCommand(targets, lookup, popups, m_sources, windowFocused, m_focus, m_focusDeclaration, event);
}

void InputCommandState::cancelSubmit(const WidgetId id)noexcept{
    CancelRouterSubmitCommands(id, m_sources);
}

b8 InputCommandState::isPressed(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const Span<const PopupRecord> popups, const b8 windowFocused, const WidgetId id)const noexcept{
    return IsRouterTargetPressedByCommand(targets, lookup, popups, m_sources, windowFocused, m_focus, m_focusDeclaration, id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

