// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "arena_names.h"

#include <global/math/vector_double.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputDispatcher::InputDispatcher()
    : m_arena(InputArenaScope::s_DispatcherArena)
    , m_handlers(m_arena)
    , m_pendingHandlerMutations(m_arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputDispatcher::addHandlerToFront(IInputEventHandler& handler){
    queueOrApplyHandlerMutation(HandlerMutationType::AddFront, handler);
}

void InputDispatcher::addHandlerToBack(IInputEventHandler& handler){
    queueOrApplyHandlerMutation(HandlerMutationType::AddBack, handler);
}

void InputDispatcher::removeHandler(IInputEventHandler& handler){
    queueOrApplyHandlerMutation(HandlerMutationType::Remove, handler);
}

void InputDispatcher::setMousePositionScale(f32 x, f32 y)noexcept{
    m_mousePositionScaleX = x != 0.f ? x : 1.f;
    m_mousePositionScaleY = y != 0.f ? y : 1.f;
}

void InputDispatcher::windowFocusUpdate(const bool focused){
    if(!focused){
        ++m_keyboardTextPolicyEpoch;
        m_keyboardTextPolicies.fill(false);
        m_keyboardTextBlocked = false;
    }
    if(m_windowFocused == focused)
        return;
    m_windowFocused = focused;

    dispatchToHandlers([focused](IInputEventHandler& handler){
        handler.windowFocusUpdate(focused);
        return false;
    });
}

void InputDispatcher::pointerLeave(){
    dispatchToHandlers([](IInputEventHandler& handler){
        handler.pointerLeave();
        return false;
    });
}

void InputDispatcher::pointerCaptureLost(){
    dispatchToHandlers([](IInputEventHandler& handler){
        handler.pointerCaptureLost();
        return false;
    });
}

void InputDispatcher::keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods){
    const bool captureText = action == InputAction::Press || action == InputAction::Repeat;
    const bool hasScancode = scancode >= 0 && static_cast<usize>(scancode) < m_keyboardTextPolicies.size();
    if(captureText){
        m_keyboardTextBlocked = false;
        if(hasScancode)
            m_keyboardTextPolicies[static_cast<usize>(scancode)] = false;
    }
    if(key == Key::Unknown)
        return;
    const u64 policyEpoch = m_keyboardTextPolicyEpoch;

    dispatchToHandlers([&](IInputEventHandler& handler){
        const bool consumed = handler.keyboardUpdate(key, scancode, action, mods);
        if(captureText && consumed){
            const bool blocked = handler.blocksKeyboardText();
            if(policyEpoch == m_keyboardTextPolicyEpoch){
                m_keyboardTextBlocked = m_windowFocused && blocked;
                if(hasScancode)
                    m_keyboardTextPolicies[static_cast<usize>(scancode)] = m_keyboardTextBlocked;
            }
        }
        // Releases reach every owner so a changed UI focus cannot leave scene input held.
        return action != InputAction::Release && consumed;
    });
}

bool InputDispatcher::keyboardTextBlocked(const i32 scancode)const noexcept{
    if(scancode >= 0 && static_cast<usize>(scancode) < m_keyboardTextPolicies.size())
        return m_keyboardTextPolicies[static_cast<usize>(scancode)];
    return m_keyboardTextBlocked;
}

void InputDispatcher::keyboardCharInput(u32 unicode, i32 mods){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.keyboardCharInput(unicode, mods);
    });
}

void InputDispatcher::mousePosUpdate(f64 xpos, f64 ypos){
    const SIMDVectorDouble position = SIMDVectorDouble{ xpos, ypos }
        / SIMDVectorDouble{ m_mousePositionScaleX, m_mousePositionScaleY };
    xpos = position.x;
    ypos = position.y;

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mousePosUpdate(xpos, ypos);
    });
}

void InputDispatcher::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    if(button == -1)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        const bool consumed = handler.mouseButtonUpdate(button, action, mods);
        return action != InputAction::Release && consumed;
    });
}

void InputDispatcher::mouseScrollUpdate(f64 xoffset, f64 yoffset){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mouseScrollUpdate(xoffset, yoffset);
    });
}

void InputDispatcher::queueOrApplyHandlerMutation(HandlerMutationType::Enum type, IInputEventHandler& handler){
    if(m_dispatchDepth > 0){
        m_pendingHandlerMutations.push_back({ &handler, type });
        if(type == HandlerMutationType::Remove)
            ++m_pendingHandlerRemovalCount;
        return;
    }

    switch(type){
    case HandlerMutationType::AddFront:
        m_handlers.remove(&handler);
        m_handlers.push_front(&handler);
        break;
    case HandlerMutationType::AddBack:
        m_handlers.remove(&handler);
        m_handlers.push_back(&handler);
        break;
    case HandlerMutationType::Remove:
        m_handlers.remove(&handler);
        break;
    }
}

void InputDispatcher::applyPendingHandlerMutations(){
    for(const HandlerMutation& mutation : m_pendingHandlerMutations){
        if(!mutation.handler)
            continue;
        queueOrApplyHandlerMutation(mutation.type, *mutation.handler);
    }
    m_pendingHandlerMutations.clear();
    m_pendingHandlerRemovalCount = 0;
}

bool InputDispatcher::isHandlerPendingRemoval(const IInputEventHandler& handler)const noexcept{
    for(const HandlerMutation& mutation : m_pendingHandlerMutations){
        if(mutation.handler == &handler && mutation.type == HandlerMutationType::Remove)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

