// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputDispatcher::setMousePositionScale(const f32 x, const f32 y)noexcept{
    m_mousePositionScaleX = x != 0.f ? x : 1.f;
    m_mousePositionScaleY = y != 0.f ? y : 1.f;
}

void InputDispatcher::windowFocusUpdate(const b8 focused){
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
    const b8 captureText = action == InputAction::Press || action == InputAction::Repeat;
    const b8 hasScancode = scancode >= 0 && static_cast<usize>(scancode) < m_keyboardTextPolicies.size();
    if(captureText){
        m_keyboardTextBlocked = false;
        if(hasScancode)
            m_keyboardTextPolicies[static_cast<usize>(scancode)] = false;
    }
    if(key == Key::Unknown)
        return;
    const u64 policyEpoch = m_keyboardTextPolicyEpoch;

    dispatchToHandlers([&](IInputEventHandler& handler){
        const KeyboardInputResult result = handler.keyboardUpdate(key, scancode, action, mods);
        if(captureText && result.consumed){
            if(policyEpoch == m_keyboardTextPolicyEpoch){
                m_keyboardTextBlocked = m_windowFocused && result.blocksText;
                if(hasScancode)
                    m_keyboardTextPolicies[static_cast<usize>(scancode)] = m_keyboardTextBlocked;
            }
        }
        return action != InputAction::Release && result.consumed;
    });
}

b8 InputDispatcher::keyboardTextBlocked(const i32 scancode)const noexcept{
    if(scancode >= 0 && static_cast<usize>(scancode) < m_keyboardTextPolicies.size())
        return m_keyboardTextPolicies[static_cast<usize>(scancode)];
    return m_keyboardTextBlocked;
}

void InputDispatcher::keyboardCharInput(const u32 unicode, const i32 mods){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.keyboardCharInput(unicode, mods);
    });
}

void InputDispatcher::mousePosUpdate(f64 xpos, f64 ypos){
    const SIMDVectorDouble position = VectorDoubleDivide(VectorDoubleSet(xpos, ypos, 0.0, 0.0), VectorDoubleSet(m_mousePositionScaleX, m_mousePositionScaleY, 1.0, 1.0));
    xpos = VectorDoubleGetX(position);
    ypos = VectorDoubleGetY(position);

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mousePosUpdate(xpos, ypos);
    });
}

void InputDispatcher::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    if(button == -1)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        const b8 consumed = handler.mouseButtonUpdate(button, action, mods);
        return action != InputAction::Release && consumed;
    });
}

void InputDispatcher::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mouseScrollUpdate(xoffset, yoffset);
    });
}

void InputDispatcher::gamepadConnectionUpdate(const u32 gamepadIndex, const b8 connected){
    if(gamepadIndex >= s_MaxGamepadCount)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        handler.gamepadConnectionUpdate(gamepadIndex, connected);
        return false;
    });
}

void InputDispatcher::gamepadButtonUpdate(const u32 gamepadIndex, const i32 button, const i32 action){
    if(gamepadIndex >= s_MaxGamepadCount || button < 0 || button >= GamepadButton::kCount)
        return;
    if(action != InputAction::Release && action != InputAction::Press && action != InputAction::Repeat)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        const b8 consumed = handler.gamepadButtonUpdate(gamepadIndex, button, action);
        return action != InputAction::Release && consumed;
    });
}

void InputDispatcher::gamepadAxisUpdate(const u32 gamepadIndex, const i32 axis, const f32 value){
    if(gamepadIndex >= s_MaxGamepadCount || axis < 0 || axis >= GamepadAxis::kCount)
        return;
    const SIMDVectorSingle scalar = VectorReplicate(value);
    if(!VectorIsFinite(scalar, VectorComponentMask::s_XY))
        return;
    const b8 isTrigger = axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
    const SIMDVectorSingle minVec = isTrigger ? VectorZero() : VectorReplicate(-1.0f);
    const f32 clamped = VectorGetX(VectorClamp(scalar, minVec, VectorSplatOne()));

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.gamepadAxisUpdate(gamepadIndex, axis, clamped);
    });
}

void InputDispatcher::queueOrApplyHandlerMutation(HandlerMutationType::Enum type, const NotNull<IInputEventHandler*> handler){
    if(m_dispatchDepth > 0){
        m_pendingHandlerMutations.push_back({ handler, type });
        if(type == HandlerMutationType::Remove)
            ++m_pendingHandlerRemovalCount;
        return;
    }

    switch(type){
    case HandlerMutationType::AddFront:
        m_handlers.remove(handler);
        m_handlers.push_front(handler);
        break;
    case HandlerMutationType::AddBack:
        m_handlers.remove(handler);
        m_handlers.push_back(handler);
        break;
    case HandlerMutationType::Remove:
        m_handlers.remove(handler);
        break;
    }
}

void InputDispatcher::applyPendingHandlerMutations(){
    for(const HandlerMutation& mutation : m_pendingHandlerMutations){
        queueOrApplyHandlerMutation(mutation.type, mutation.handler);
    }
    m_pendingHandlerMutations.clear();
    m_pendingHandlerRemovalCount = 0;
}

b8 InputDispatcher::isHandlerPendingRemoval(const IInputEventHandler* const handler)const noexcept{
    for(const HandlerMutation& mutation : m_pendingHandlerMutations){
        if(mutation.handler.get() == handler && mutation.type == HandlerMutationType::Remove)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
