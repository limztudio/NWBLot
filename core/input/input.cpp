// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


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

void InputDispatcher::setMousePositionScale(f32 x, f32 y){
    m_mousePositionScaleX = x != 0.f ? x : 1.f;
    m_mousePositionScaleY = y != 0.f ? y : 1.f;
}

void InputDispatcher::keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods){
    if(key == Key::Unknown)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.keyboardUpdate(key, scancode, action, mods);
    });
}

void InputDispatcher::keyboardCharInput(u32 unicode, i32 mods){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.keyboardCharInput(unicode, mods);
    });
}

void InputDispatcher::mousePosUpdate(f64 xpos, f64 ypos){
    xpos /= m_mousePositionScaleX;
    ypos /= m_mousePositionScaleY;

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mousePosUpdate(xpos, ypos);
    });
}

void InputDispatcher::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    if(button == -1)
        return;

    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mouseButtonUpdate(button, action, mods);
    });
}

void InputDispatcher::mouseScrollUpdate(f64 xoffset, f64 yoffset){
    dispatchToHandlers([&](IInputEventHandler& handler){
        return handler.mouseScrollUpdate(xoffset, yoffset);
    });
}

void InputDispatcher::queueOrApplyHandlerMutation(HandlerMutationType type, IInputEventHandler& handler){
    if(m_dispatchDepth > 0){
        m_pendingHandlerMutations.push_back({ type, &handler });
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
}

bool InputDispatcher::isHandlerPendingRemoval(const IInputEventHandler& handler)const{
    for(const HandlerMutation& mutation : m_pendingHandlerMutations){
        if(mutation.handler == &handler && mutation.type == HandlerMutationType::Remove)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
