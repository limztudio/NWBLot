
#pragma once

#include <global/core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ArrowYawInputHandler final : public Core::IInputEventHandler{
public:
    bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override{
        static_cast<void>(scancode);
        static_cast<void>(mods);

        const bool held = (action != Core::InputAction::Release);
        switch(key){
        case Core::Key::Left:  m_leftHeld = held;  return true;
        case Core::Key::Right: m_rightHeld = held; return true;
        default:               return false;
        }
    }

    [[nodiscard]] f32 axis()const{
        return (m_rightHeld ? 1.0f : 0.0f) - (m_leftHeld ? 1.0f : 0.0f);
    }


private:
    bool m_leftHeld = false;
    bool m_rightHeld = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

