// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputAction{
    static constexpr auto s_InputActionReleaseBase = 0;
    enum Enum : i32{
        Release = s_InputActionReleaseBase,
        Press,
        Repeat,
    };
};

namespace InputModifier{
    static constexpr i32 s_ModifierShiftBits = 0x0001;
    static constexpr i32 s_ModifierControlBits = 0x0002;
    static constexpr i32 s_ModifierAltBits = 0x0004;
    static constexpr i32 s_ModifierSuperBits = 0x0008;
    static constexpr i32 s_ModifierCapsLockBits = 0x0010;
    static constexpr i32 s_ModifierNumLockBits = 0x0020;
    enum Enum : i32{
        Shift = s_ModifierShiftBits,
        Control = s_ModifierControlBits,
        Alt = s_ModifierAltBits,
        Super = s_ModifierSuperBits,
        CapsLock = s_ModifierCapsLockBits,
        NumLock = s_ModifierNumLockBits,
    };
};

namespace MouseButton{
    static constexpr auto s_MouseButtonLeftBase = 0;
    enum Enum : i32{
        Left = s_MouseButtonLeftBase,
        Right,
        Middle,
        Button4,
        Button5,
        Button6,
        Button7,
        Button8,
    };
};

namespace Key{
    static constexpr i32 s_KeyUnknownCode = -1;
    static constexpr i32 s_KeySpaceAscii = 32;
    static constexpr i32 s_KeyWorld1Code = 161;
    static constexpr i32 s_KeyWorld2Code = 162;
    static constexpr i32 s_KeyLeftShiftCode = 340;
    static constexpr i32 s_KeyKeypad0Code = 320;
    static constexpr i32 s_KeyF1Code = 290;
    static constexpr i32 s_KeyCapsLockCode = 280;
    static constexpr i32 s_KeyEscapeCode = 256;
    static constexpr i32 s_KeyGraveAccentCode = 96;
    static constexpr i32 s_KeyLeftBracketCode = 91;
    static constexpr i32 s_KeyACode = 65;
    static constexpr i32 s_KeyEqualCode = 61;
    static constexpr i32 s_KeySemicolonCode = 59;
    static constexpr i32 s_KeyCommaCode = 44;
    static constexpr i32 s_KeyApostropheCode = 39;
    enum Enum : i32{
        Unknown = s_KeyUnknownCode,

        Space = s_KeySpaceAscii,
        Apostrophe = s_KeyApostropheCode,
        Comma = s_KeyCommaCode,
        Minus,
        Period,
        Slash,
        Number0,
        Number1,
        Number2,
        Number3,
        Number4,
        Number5,
        Number6,
        Number7,
        Number8,
        Number9,
        Semicolon = s_KeySemicolonCode,
        Equal = s_KeyEqualCode,
        A = s_KeyACode,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        LeftBracket = s_KeyLeftBracketCode,
        Backslash,
        RightBracket,
        GraveAccent = s_KeyGraveAccentCode,
        World1 = s_KeyWorld1Code,
        World2,

        Escape = s_KeyEscapeCode,
        Enter,
        Tab,
        Backspace,
        Insert,
        Delete,
        Right,
        Left,
        Down,
        Up,
        PageUp,
        PageDown,
        Home,
        End,
        CapsLock = s_KeyCapsLockCode,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,
        F1 = s_KeyF1Code,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
        F13,
        F14,
        F15,
        F16,
        F17,
        F18,
        F19,
        F20,
        F21,
        F22,
        F23,
        F24,
        F25,
        Keypad0 = s_KeyKeypad0Code,
        Keypad1,
        Keypad2,
        Keypad3,
        Keypad4,
        Keypad5,
        Keypad6,
        Keypad7,
        Keypad8,
        Keypad9,
        KeypadDecimal,
        KeypadDivide,
        KeypadMultiply,
        KeypadSubtract,
        KeypadAdd,
        KeypadEnter,
        KeypadEqual,
        LeftShift = s_KeyLeftShiftCode,
        LeftControl,
        LeftAlt,
        LeftSuper,
        RightShift,
        RightControl,
        RightAlt,
        RightSuper,
        Menu,
    };
};

static_assert(Key::World1 == Key::s_KeyWorld1Code && Key::World2 == Key::s_KeyWorld2Code);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct KeyboardInputResult{
    b8 consumed = false;
    b8 blocksText = false;

    constexpr KeyboardInputResult(const b8 consumedValue = false, const b8 blocksTextValue = false)noexcept
        : consumed(consumedValue), blocksText(blocksTextValue){}
};

interface IInputEventHandler{
public:
    virtual ~IInputEventHandler()noexcept = default;


public:
    virtual void windowFocusUpdate(b8){}

    virtual void pointerLeave(){}

    virtual void pointerCaptureLost(){}

    virtual KeyboardInputResult keyboardUpdate(i32, i32, i32, i32){ return false; }

    virtual b8 keyboardCharInput(u32, i32){ return false; }

    virtual b8 mousePosUpdate(f64, f64){ return false; }

    virtual b8 mouseButtonUpdate(i32, i32, i32){ return false; }

    virtual b8 mouseScrollUpdate(f64, f64){ return false; }
};


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
