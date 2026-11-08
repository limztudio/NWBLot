// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fly_camera_input.h"
#include "CoolStuff/Testbed/fly_camera_motion.adhd.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FlyCameraInput::clear()noexcept{
    m_keyPressed.fill(false);
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_lastMouseX = 0.0;
    m_lastMouseY = 0.0;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
}

void FlyCameraInput::pointerCaptureLost()noexcept{
    // Keep motion from an already completed gesture.
    if(!m_mouseLookActive)
        return;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
}

void FlyCameraInput::updatePointer(const f64 xpos, const f64 ypos, const b8 wantsPointer)noexcept{
    FlyCameraMotion().updateMousePosition(
        xpos, ypos, wantsPointer, m_mouseLookActive, m_mousePositionValid,
        m_lastMouseX, m_lastMouseY, m_pendingMouseDeltaX, m_pendingMouseDeltaY
    );
}

void FlyCameraInput::updateButton(const b8 wantsPointer, const b8 isRightButton, const b8 isPress, const b8 isRelease)noexcept{
    FlyCameraMotion().updateMouseButton(wantsPointer, isRightButton, isPress, isRelease, m_mouseLookActive, m_mousePositionValid);
}

FlyCameraPointerDelta FlyCameraInput::takePointerDelta(const b8 pointerCaptured)noexcept{
    const FlyCameraPointerDelta result{ m_pendingMouseDeltaX, m_pendingMouseDeltaY };
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    if(pointerCaptured){
        m_mouseLookActive = false;
        m_mousePositionValid = false;
    }
    return result;
}

void FlyCameraInput::setKeyState(const i32 key, const b8 pressed)noexcept{
    if(key < 0 || key > NWB::Core::Key::Menu)
        return;
    m_keyPressed[static_cast<usize>(key)] = pressed;
}

b8 FlyCameraInput::keyPressed(const i32 key)const noexcept{
    if(key < 0 || key > NWB::Core::Key::Menu)
        return false;
    return m_keyPressed[static_cast<usize>(key)];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


