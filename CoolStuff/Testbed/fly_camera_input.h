// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <core/input/module.h>

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FlyCameraPointerDelta{
    f32 x = 0.0f;
    f32 y = 0.0f;
};

class FlyCameraInput final{
private:
    static constexpr usize s_KeyStateCount = static_cast<usize>(NWB::Core::Key::Menu) + 1u;


public:
    void clear()noexcept;
    void pointerCaptureLost()noexcept;
    void updatePointer(f64 xpos, f64 ypos, b8 wantsPointer)noexcept;
    void updateButton(b8 wantsPointer, b8 isRightButton, b8 isPress, b8 isRelease)noexcept;
    [[nodiscard]] FlyCameraPointerDelta takePointerDelta(b8 pointerCaptured)noexcept;
    void setKeyState(i32 key, b8 pressed)noexcept;
    [[nodiscard]] b8 keyPressed(i32 key)const noexcept;


private:
    f64 m_lastMouseX = 0.0;
    f64 m_lastMouseY = 0.0;
    f32 m_pendingMouseDeltaX = 0.0f;
    f32 m_pendingMouseDeltaY = 0.0f;
    b8 m_mouseLookActive = false;
    b8 m_mousePositionValid = false;
    Array<b8, s_KeyStateCount> m_keyPressed = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


