// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


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

#define NWB_DECLARE_LINUX_KEY_SYMBOLS(StructName, ValueType, Prefix) \
struct StructName{ \
    using value_type = ValueType; \
    static constexpr value_type s_Key0 = Prefix##_0; \
    static constexpr value_type s_Key9 = Prefix##_9; \
    static constexpr value_type s_KeyA = Prefix##_A; \
    static constexpr value_type s_KeyZ = Prefix##_Z; \
    static constexpr value_type s_Keya = Prefix##_a; \
    static constexpr value_type s_Keyz = Prefix##_z; \
    static constexpr value_type s_F1 = Prefix##_F1; \
    static constexpr value_type s_F24 = Prefix##_F24; \
    static constexpr value_type s_KP0 = Prefix##_KP_0; \
    static constexpr value_type s_KP9 = Prefix##_KP_9; \
    static constexpr value_type s_ISOLeftTab = Prefix##_ISO_Left_Tab; \
    static constexpr value_type s_Grave = Prefix##_grave; \
    static constexpr value_type s_QuoteLeft = Prefix##_quoteleft; \
    static constexpr value_type s_Print = Prefix##_Print; \
    static constexpr value_type s_SysReq = Prefix##_Sys_Req; \
    static constexpr value_type s_AltL = Prefix##_Alt_L; \
    static constexpr value_type s_MetaL = Prefix##_Meta_L; \
    static constexpr value_type s_AltR = Prefix##_Alt_R; \
    static constexpr value_type s_MetaR = Prefix##_Meta_R; \
    static constexpr value_type s_SuperL = Prefix##_Super_L; \
    static constexpr value_type s_HyperL = Prefix##_Hyper_L; \
    static constexpr value_type s_SuperR = Prefix##_Super_R; \
    static constexpr value_type s_HyperR = Prefix##_Hyper_R; \
    static constexpr value_type s_Space = Prefix##_space; \
    static constexpr value_type s_Apostrophe = Prefix##_apostrophe; \
    static constexpr value_type s_Comma = Prefix##_comma; \
    static constexpr value_type s_Minus = Prefix##_minus; \
    static constexpr value_type s_Period = Prefix##_period; \
    static constexpr value_type s_Slash = Prefix##_slash; \
    static constexpr value_type s_Semicolon = Prefix##_semicolon; \
    static constexpr value_type s_Equal = Prefix##_equal; \
    static constexpr value_type s_BracketLeft = Prefix##_bracketleft; \
    static constexpr value_type s_Backslash = Prefix##_backslash; \
    static constexpr value_type s_BracketRight = Prefix##_bracketright; \
    static constexpr value_type s_Escape = Prefix##_Escape; \
    static constexpr value_type s_ReturnKey = Prefix##_Return; \
    static constexpr value_type s_Tab = Prefix##_Tab; \
    static constexpr value_type s_BackSpace = Prefix##_BackSpace; \
    static constexpr value_type s_Insert = Prefix##_Insert; \
    static constexpr value_type s_DeleteKey = Prefix##_Delete; \
    static constexpr value_type s_Right = Prefix##_Right; \
    static constexpr value_type s_Left = Prefix##_Left; \
    static constexpr value_type s_Down = Prefix##_Down; \
    static constexpr value_type s_Up = Prefix##_Up; \
    static constexpr value_type s_Prior = Prefix##_Prior; \
    static constexpr value_type s_Next = Prefix##_Next; \
    static constexpr value_type s_Home = Prefix##_Home; \
    static constexpr value_type s_End = Prefix##_End; \
    static constexpr value_type s_CapsLock = Prefix##_Caps_Lock; \
    static constexpr value_type s_ScrollLock = Prefix##_Scroll_Lock; \
    static constexpr value_type s_NumLock = Prefix##_Num_Lock; \
    static constexpr value_type s_Pause = Prefix##_Pause; \
    static constexpr value_type s_F25 = Prefix##_F25; \
    static constexpr value_type s_KPInsert = Prefix##_KP_Insert; \
    static constexpr value_type s_KPEnd = Prefix##_KP_End; \
    static constexpr value_type s_KPDown = Prefix##_KP_Down; \
    static constexpr value_type s_KPNext = Prefix##_KP_Next; \
    static constexpr value_type s_KPLeft = Prefix##_KP_Left; \
    static constexpr value_type s_KPBegin = Prefix##_KP_Begin; \
    static constexpr value_type s_KPRight = Prefix##_KP_Right; \
    static constexpr value_type s_KPHome = Prefix##_KP_Home; \
    static constexpr value_type s_KPUp = Prefix##_KP_Up; \
    static constexpr value_type s_KPPrior = Prefix##_KP_Prior; \
    static constexpr value_type s_KPDelete = Prefix##_KP_Delete; \
    static constexpr value_type s_KPDecimal = Prefix##_KP_Decimal; \
    static constexpr value_type s_KPDivide = Prefix##_KP_Divide; \
    static constexpr value_type s_KPMultiply = Prefix##_KP_Multiply; \
    static constexpr value_type s_KPSubtract = Prefix##_KP_Subtract; \
    static constexpr value_type s_KPAdd = Prefix##_KP_Add; \
    static constexpr value_type s_KPEnter = Prefix##_KP_Enter; \
    static constexpr value_type s_KPEqual = Prefix##_KP_Equal; \
    static constexpr value_type s_ShiftL = Prefix##_Shift_L; \
    static constexpr value_type s_ControlL = Prefix##_Control_L; \
    static constexpr value_type s_ShiftR = Prefix##_Shift_R; \
    static constexpr value_type s_ControlR = Prefix##_Control_R; \
    static constexpr value_type s_Menu = Prefix##_Menu; \
}

template<typename Symbols>
[[nodiscard]] inline i32 TranslateLinuxKeySymbol(typename Symbols::value_type keySym){
    if(keySym >= Symbols::s_Key0 && keySym <= Symbols::s_Key9)
        return static_cast<i32>(keySym);

    if(keySym >= Symbols::s_KeyA && keySym <= Symbols::s_KeyZ)
        return static_cast<i32>(keySym);

    if(keySym >= Symbols::s_Keya && keySym <= Symbols::s_Keyz)
        return static_cast<i32>(Key::A + (keySym - Symbols::s_Keya));

    if(keySym >= Symbols::s_F1 && keySym <= Symbols::s_F24)
        return static_cast<i32>(Key::F1 + (keySym - Symbols::s_F1));

    if(keySym >= Symbols::s_KP0 && keySym <= Symbols::s_KP9)
        return static_cast<i32>(Key::Keypad0 + (keySym - Symbols::s_KP0));

    if(keySym == Symbols::s_ISOLeftTab)
        return Key::Tab;
    if(keySym == Symbols::s_Grave || keySym == Symbols::s_QuoteLeft)
        return Key::GraveAccent;
    if(keySym == Symbols::s_Print || keySym == Symbols::s_SysReq)
        return Key::PrintScreen;
    if(keySym == Symbols::s_AltL || keySym == Symbols::s_MetaL)
        return Key::LeftAlt;
    if(keySym == Symbols::s_AltR || keySym == Symbols::s_MetaR)
        return Key::RightAlt;
    if(keySym == Symbols::s_SuperL || keySym == Symbols::s_HyperL)
        return Key::LeftSuper;
    if(keySym == Symbols::s_SuperR || keySym == Symbols::s_HyperR)
        return Key::RightSuper;

    switch(keySym){
    case Symbols::s_Space: return Key::Space;
    case Symbols::s_Apostrophe: return Key::Apostrophe;
    case Symbols::s_Comma: return Key::Comma;
    case Symbols::s_Minus: return Key::Minus;
    case Symbols::s_Period: return Key::Period;
    case Symbols::s_Slash: return Key::Slash;
    case Symbols::s_Semicolon: return Key::Semicolon;
    case Symbols::s_Equal: return Key::Equal;
    case Symbols::s_BracketLeft: return Key::LeftBracket;
    case Symbols::s_Backslash: return Key::Backslash;
    case Symbols::s_BracketRight: return Key::RightBracket;

    case Symbols::s_Escape: return Key::Escape;
    case Symbols::s_ReturnKey: return Key::Enter;
    case Symbols::s_Tab: return Key::Tab;
    case Symbols::s_BackSpace: return Key::Backspace;
    case Symbols::s_Insert: return Key::Insert;
    case Symbols::s_DeleteKey: return Key::Delete;
    case Symbols::s_Right: return Key::Right;
    case Symbols::s_Left: return Key::Left;
    case Symbols::s_Down: return Key::Down;
    case Symbols::s_Up: return Key::Up;
    case Symbols::s_Prior: return Key::PageUp;
    case Symbols::s_Next: return Key::PageDown;
    case Symbols::s_Home: return Key::Home;
    case Symbols::s_End: return Key::End;
    case Symbols::s_CapsLock: return Key::CapsLock;
    case Symbols::s_ScrollLock: return Key::ScrollLock;
    case Symbols::s_NumLock: return Key::NumLock;
    case Symbols::s_Pause: return Key::Pause;
    case Symbols::s_F25: return Key::F25;

    case Symbols::s_KPInsert: return Key::Keypad0;
    case Symbols::s_KPEnd: return Key::Keypad1;
    case Symbols::s_KPDown: return Key::Keypad2;
    case Symbols::s_KPNext: return Key::Keypad3;
    case Symbols::s_KPLeft: return Key::Keypad4;
    case Symbols::s_KPBegin: return Key::Keypad5;
    case Symbols::s_KPRight: return Key::Keypad6;
    case Symbols::s_KPHome: return Key::Keypad7;
    case Symbols::s_KPUp: return Key::Keypad8;
    case Symbols::s_KPPrior: return Key::Keypad9;
    case Symbols::s_KPDelete: return Key::KeypadDecimal;
    case Symbols::s_KPDecimal: return Key::KeypadDecimal;
    case Symbols::s_KPDivide: return Key::KeypadDivide;
    case Symbols::s_KPMultiply: return Key::KeypadMultiply;
    case Symbols::s_KPSubtract: return Key::KeypadSubtract;
    case Symbols::s_KPAdd: return Key::KeypadAdd;
    case Symbols::s_KPEnter: return Key::KeypadEnter;
    case Symbols::s_KPEqual: return Key::KeypadEqual;

    case Symbols::s_ShiftL: return Key::LeftShift;
    case Symbols::s_ControlL: return Key::LeftControl;
    case Symbols::s_ShiftR: return Key::RightShift;
    case Symbols::s_ControlR: return Key::RightControl;
    case Symbols::s_Menu: return Key::Menu;
    default:
        return Key::Unknown;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

