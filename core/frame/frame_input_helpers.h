// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline i32 AdjustModifiersForKey(i32 key, i32 action, i32 mods){
    switch(key){
    case Key::LeftShift:
    case Key::RightShift:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Shift)
            : (mods | InputModifier::Shift)
        ;
    case Key::LeftControl:
    case Key::RightControl:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Control)
            : (mods | InputModifier::Control)
        ;
    case Key::LeftAlt:
    case Key::RightAlt:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Alt)
            : (mods | InputModifier::Alt)
        ;
    case Key::LeftSuper:
    case Key::RightSuper:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Super)
            : (mods | InputModifier::Super)
        ;
    default:
        return mods;
    }
}

template<typename Symbols>
[[nodiscard]] inline i32 TranslateLinuxKeySymbol(typename Symbols::value_type keySym){
    if(keySym >= Symbols::Key0 && keySym <= Symbols::Key9)
        return static_cast<i32>(keySym);

    if(keySym >= Symbols::KeyA && keySym <= Symbols::KeyZ)
        return static_cast<i32>(keySym);

    if(keySym >= Symbols::Keya && keySym <= Symbols::Keyz)
        return static_cast<i32>(Key::A + (keySym - Symbols::Keya));

    if(keySym >= Symbols::F1 && keySym <= Symbols::F24)
        return static_cast<i32>(Key::F1 + (keySym - Symbols::F1));

    if(keySym >= Symbols::KP0 && keySym <= Symbols::KP9)
        return static_cast<i32>(Key::Keypad0 + (keySym - Symbols::KP0));

    if(keySym == Symbols::ISOLeftTab)
        return Key::Tab;
    if(keySym == Symbols::Grave || keySym == Symbols::QuoteLeft)
        return Key::GraveAccent;
    if(keySym == Symbols::Print || keySym == Symbols::SysReq)
        return Key::PrintScreen;
    if(keySym == Symbols::AltL || keySym == Symbols::MetaL)
        return Key::LeftAlt;
    if(keySym == Symbols::AltR || keySym == Symbols::MetaR)
        return Key::RightAlt;
    if(keySym == Symbols::SuperL || keySym == Symbols::HyperL)
        return Key::LeftSuper;
    if(keySym == Symbols::SuperR || keySym == Symbols::HyperR)
        return Key::RightSuper;

    switch(keySym){
    case Symbols::Space: return Key::Space;
    case Symbols::Apostrophe: return Key::Apostrophe;
    case Symbols::Comma: return Key::Comma;
    case Symbols::Minus: return Key::Minus;
    case Symbols::Period: return Key::Period;
    case Symbols::Slash: return Key::Slash;
    case Symbols::Semicolon: return Key::Semicolon;
    case Symbols::Equal: return Key::Equal;
    case Symbols::BracketLeft: return Key::LeftBracket;
    case Symbols::Backslash: return Key::Backslash;
    case Symbols::BracketRight: return Key::RightBracket;

    case Symbols::Escape: return Key::Escape;
    case Symbols::ReturnKey: return Key::Enter;
    case Symbols::Tab: return Key::Tab;
    case Symbols::BackSpace: return Key::Backspace;
    case Symbols::Insert: return Key::Insert;
    case Symbols::DeleteKey: return Key::Delete;
    case Symbols::Right: return Key::Right;
    case Symbols::Left: return Key::Left;
    case Symbols::Down: return Key::Down;
    case Symbols::Up: return Key::Up;
    case Symbols::Prior: return Key::PageUp;
    case Symbols::Next: return Key::PageDown;
    case Symbols::Home: return Key::Home;
    case Symbols::End: return Key::End;
    case Symbols::CapsLock: return Key::CapsLock;
    case Symbols::ScrollLock: return Key::ScrollLock;
    case Symbols::NumLock: return Key::NumLock;
    case Symbols::Pause: return Key::Pause;
    case Symbols::F25: return Key::F25;

    case Symbols::KPInsert: return Key::Keypad0;
    case Symbols::KPEnd: return Key::Keypad1;
    case Symbols::KPDown: return Key::Keypad2;
    case Symbols::KPNext: return Key::Keypad3;
    case Symbols::KPLeft: return Key::Keypad4;
    case Symbols::KPBegin: return Key::Keypad5;
    case Symbols::KPRight: return Key::Keypad6;
    case Symbols::KPHome: return Key::Keypad7;
    case Symbols::KPUp: return Key::Keypad8;
    case Symbols::KPPrior: return Key::Keypad9;
    case Symbols::KPDelete: return Key::KeypadDecimal;
    case Symbols::KPDecimal: return Key::KeypadDecimal;
    case Symbols::KPDivide: return Key::KeypadDivide;
    case Symbols::KPMultiply: return Key::KeypadMultiply;
    case Symbols::KPSubtract: return Key::KeypadSubtract;
    case Symbols::KPAdd: return Key::KeypadAdd;
    case Symbols::KPEnter: return Key::KeypadEnter;
    case Symbols::KPEqual: return Key::KeypadEqual;

    case Symbols::ShiftL: return Key::LeftShift;
    case Symbols::ControlL: return Key::LeftControl;
    case Symbols::ShiftR: return Key::RightShift;
    case Symbols::ControlR: return Key::RightControl;
    case Symbols::Menu: return Key::Menu;
    default:
        return Key::Unknown;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

