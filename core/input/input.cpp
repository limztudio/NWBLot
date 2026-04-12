// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputDispatcher::addHandlerToFront(IInputEventHandler& handler){
    m_handlers.remove(&handler);
    m_handlers.push_front(&handler);
}

void InputDispatcher::addHandlerToBack(IInputEventHandler& handler){
    m_handlers.remove(&handler);
    m_handlers.push_back(&handler);
}

void InputDispatcher::removeHandler(IInputEventHandler& handler){
    m_handlers.remove(&handler);
}

void InputDispatcher::setMousePositionScale(f32 x, f32 y){
    m_mousePositionScaleX = x != 0.f ? x : 1.f;
    m_mousePositionScaleY = y != 0.f ? y : 1.f;
}

void InputDispatcher::keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods){
    if(key == Key::Unknown)
        return;

    for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
        if((*it)->keyboardUpdate(key, scancode, action, mods))
            break;
    }
}

void InputDispatcher::keyboardCharInput(u32 unicode, i32 mods){
    for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
        if((*it)->keyboardCharInput(unicode, mods))
            break;
    }
}

void InputDispatcher::mousePosUpdate(f64 xpos, f64 ypos){
    xpos /= m_mousePositionScaleX;
    ypos /= m_mousePositionScaleY;

    for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
        if((*it)->mousePosUpdate(xpos, ypos))
            break;
    }
}

void InputDispatcher::mouseButtonUpdate(i32 button, i32 action, i32 mods){
    if(button == -1)
        return;

    for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
        if((*it)->mouseButtonUpdate(button, action, mods))
            break;
    }
}

void InputDispatcher::mouseScrollUpdate(f64 xoffset, f64 yoffset){
    for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
        if((*it)->mouseScrollUpdate(xoffset, yoffset))
            break;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
