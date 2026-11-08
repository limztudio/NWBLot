// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pointer_state.h"
#include "command_state.h"
#include "popup_state.h"
#include "target_table.h"

#include <impl/ecs_ui/toolkit/input/router_pointer_source_admission.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputPointerState::InputPointerState(Core::Alloc::GlobalArena& arena)
    : m_pointerSources(arena)
    , m_pointerGestures(arena)
{}

void InputPointerState::reserveSources(){
    m_pointerSources.reserve(s_InputSourceReserve);
}

void InputPointerState::reserveGestures(){
    m_pointerGestures.reserve(s_InputMaxPointerGestures);
}

b8 InputPointerState::accepts(const InputSource& source)const noexcept{
    return AcceptsRouterPointerSource(m_activePointerSource, m_pointerSources, source);
}

b8 InputPointerState::owns(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    return OwnsRouterPointerSource(m_pointerSources, source, intent);
}

b8 InputPointerState::wants(const b8 hasPopup, const WidgetId hover)const noexcept{
    return WantsRouterPointer(m_pointerSources, hasPopup, hover);
}

InputPointerRouteEffects InputPointerState::routePrimary(const InputEvent& event, const HitTarget* const hit, InputTargetTable& targets, InputPopupState& popups, InputCommandState& commands, Vector<InputAction, Core::Alloc::GlobalArena>& actions, Vector<ControlAction, Core::Alloc::GlobalArena>& controlActions, u64& nextActionSequence, InputRoutingResult& result){
    InputPointerRouteEffects effects;
    effects.ok = RouteRouterPrimary(
        targets.targets(),
        targets.lookup(),
        event,
        hit != nullptr,
        hit != nullptr ? *hit : HitTarget{},
        m_secondaryDown,
        m_secondarySequenceConsumed,
        targets.generation(),
        m_activePointerSource,
        m_interactionPosition,
        popups.records(),
        popups.canAppendDismissal(),
        effects.popupDismissal,
        commands.records(),
        actions,
        controlActions,
        m_pointerGestures,
        m_activeGestureSequence,
        nextActionSequence,
        m_primaryDown,
        m_pointerSubmitArmed,
        m_pointerSequenceConsumed,
        commands.focus(),
        commands.declaration(),
        commands.control(),
        m_capture,
        m_capturePopup,
        m_captureDeclaration,
        m_captureControl,
        result,
        effects.advanceHover,
        effects.dismissedPopup
    );
    return effects;
}

InputPointerRouteEffects InputPointerState::routeSecondary(const InputEvent& event, const HitTarget* const hit, InputTargetTable& targets, InputPopupState& popups, const b8 windowFocused, Vector<ContextMenuAction, Core::Alloc::GlobalArena>& contextMenuActions, u64& nextActionSequence, InputRoutingResult& result){
    InputPointerRouteEffects effects;
    RouteRouterSecondary(
        targets.targets(),
        targets.lookup(),
        windowFocused,
        targets.generation(),
        event,
        hit != nullptr,
        hit != nullptr ? *hit : HitTarget{},
        m_primaryDown,
        m_pointerSequenceConsumed,
        popups.records(),
        popups.canAppendDismissal(),
        effects.popupDismissal,
        contextMenuActions,
        nextActionSequence,
        m_secondaryDown,
        m_secondarySequenceConsumed,
        m_secondaryOwner,
        m_secondaryPosition,
        result,
        effects.advanceHover,
        effects.dismissedPopup
    );
    return effects;
}

void InputPointerState::routeWheel(const InputEvent& event, const HitTarget* const hit, InputTargetTable& targets, InputPopupState& popups, Vector<ControlAction, Core::Alloc::GlobalArena>& controlActions, u64& nextActionSequence, InputRoutingResult& result){
    RouteRouterWheel(
        targets.targets(),
        targets.lookup(),
        popups.records(),
        event,
        hit != nullptr,
        hit != nullptr ? *hit : HitTarget{},
        m_primaryDown,
        m_pointerSequenceConsumed,
        m_secondaryDown,
        m_secondarySequenceConsumed,
        targets.generation(),
        controlActions,
        nextActionSequence,
        result
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

