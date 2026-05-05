// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline i32 AdjustModifiersForKey(i32 key, i32 action, i32 mods){
    switch(key){
    case Key::LeftShift:
    case Key::RightShift:
        return
            action == InputAction::Release
            ? (mods & ~InputModifier::Shift)
            : (mods | InputModifier::Shift)
        ;
    case Key::LeftControl:
    case Key::RightControl:
        return
            action == InputAction::Release
            ? (mods & ~InputModifier::Control)
            : (mods | InputModifier::Control)
        ;
    case Key::LeftAlt:
    case Key::RightAlt:
        return
            action == InputAction::Release
            ? (mods & ~InputModifier::Alt)
            : (mods | InputModifier::Alt)
        ;
    case Key::LeftSuper:
    case Key::RightSuper:
        return
            action == InputAction::Release
            ? (mods & ~InputModifier::Super)
            : (mods | InputModifier::Super)
        ;
    default:
        return mods;
    }
}

#define NWB_DECLARE_LINUX_KEY_SYMBOLS(StructName, ValueType, Prefix) \
struct StructName{ \
    using value_type = ValueType; \
    static constexpr value_type Key0 = Prefix##_0; \
    static constexpr value_type Key9 = Prefix##_9; \
    static constexpr value_type KeyA = Prefix##_A; \
    static constexpr value_type KeyZ = Prefix##_Z; \
    static constexpr value_type Keya = Prefix##_a; \
    static constexpr value_type Keyz = Prefix##_z; \
    static constexpr value_type F1 = Prefix##_F1; \
    static constexpr value_type F24 = Prefix##_F24; \
    static constexpr value_type KP0 = Prefix##_KP_0; \
    static constexpr value_type KP9 = Prefix##_KP_9; \
    static constexpr value_type ISOLeftTab = Prefix##_ISO_Left_Tab; \
    static constexpr value_type Grave = Prefix##_grave; \
    static constexpr value_type QuoteLeft = Prefix##_quoteleft; \
    static constexpr value_type Print = Prefix##_Print; \
    static constexpr value_type SysReq = Prefix##_Sys_Req; \
    static constexpr value_type AltL = Prefix##_Alt_L; \
    static constexpr value_type MetaL = Prefix##_Meta_L; \
    static constexpr value_type AltR = Prefix##_Alt_R; \
    static constexpr value_type MetaR = Prefix##_Meta_R; \
    static constexpr value_type SuperL = Prefix##_Super_L; \
    static constexpr value_type HyperL = Prefix##_Hyper_L; \
    static constexpr value_type SuperR = Prefix##_Super_R; \
    static constexpr value_type HyperR = Prefix##_Hyper_R; \
    static constexpr value_type Space = Prefix##_space; \
    static constexpr value_type Apostrophe = Prefix##_apostrophe; \
    static constexpr value_type Comma = Prefix##_comma; \
    static constexpr value_type Minus = Prefix##_minus; \
    static constexpr value_type Period = Prefix##_period; \
    static constexpr value_type Slash = Prefix##_slash; \
    static constexpr value_type Semicolon = Prefix##_semicolon; \
    static constexpr value_type Equal = Prefix##_equal; \
    static constexpr value_type BracketLeft = Prefix##_bracketleft; \
    static constexpr value_type Backslash = Prefix##_backslash; \
    static constexpr value_type BracketRight = Prefix##_bracketright; \
    static constexpr value_type Escape = Prefix##_Escape; \
    static constexpr value_type ReturnKey = Prefix##_Return; \
    static constexpr value_type Tab = Prefix##_Tab; \
    static constexpr value_type BackSpace = Prefix##_BackSpace; \
    static constexpr value_type Insert = Prefix##_Insert; \
    static constexpr value_type DeleteKey = Prefix##_Delete; \
    static constexpr value_type Right = Prefix##_Right; \
    static constexpr value_type Left = Prefix##_Left; \
    static constexpr value_type Down = Prefix##_Down; \
    static constexpr value_type Up = Prefix##_Up; \
    static constexpr value_type Prior = Prefix##_Prior; \
    static constexpr value_type Next = Prefix##_Next; \
    static constexpr value_type Home = Prefix##_Home; \
    static constexpr value_type End = Prefix##_End; \
    static constexpr value_type CapsLock = Prefix##_Caps_Lock; \
    static constexpr value_type ScrollLock = Prefix##_Scroll_Lock; \
    static constexpr value_type NumLock = Prefix##_Num_Lock; \
    static constexpr value_type Pause = Prefix##_Pause; \
    static constexpr value_type F25 = Prefix##_F25; \
    static constexpr value_type KPInsert = Prefix##_KP_Insert; \
    static constexpr value_type KPEnd = Prefix##_KP_End; \
    static constexpr value_type KPDown = Prefix##_KP_Down; \
    static constexpr value_type KPNext = Prefix##_KP_Next; \
    static constexpr value_type KPLeft = Prefix##_KP_Left; \
    static constexpr value_type KPBegin = Prefix##_KP_Begin; \
    static constexpr value_type KPRight = Prefix##_KP_Right; \
    static constexpr value_type KPHome = Prefix##_KP_Home; \
    static constexpr value_type KPUp = Prefix##_KP_Up; \
    static constexpr value_type KPPrior = Prefix##_KP_Prior; \
    static constexpr value_type KPDelete = Prefix##_KP_Delete; \
    static constexpr value_type KPDecimal = Prefix##_KP_Decimal; \
    static constexpr value_type KPDivide = Prefix##_KP_Divide; \
    static constexpr value_type KPMultiply = Prefix##_KP_Multiply; \
    static constexpr value_type KPSubtract = Prefix##_KP_Subtract; \
    static constexpr value_type KPAdd = Prefix##_KP_Add; \
    static constexpr value_type KPEnter = Prefix##_KP_Enter; \
    static constexpr value_type KPEqual = Prefix##_KP_Equal; \
    static constexpr value_type ShiftL = Prefix##_Shift_L; \
    static constexpr value_type ControlL = Prefix##_Control_L; \
    static constexpr value_type ShiftR = Prefix##_Shift_R; \
    static constexpr value_type ControlR = Prefix##_Control_R; \
    static constexpr value_type Menu = Prefix##_Menu; \
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

