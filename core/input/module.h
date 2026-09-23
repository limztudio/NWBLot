// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputAction{
    static constexpr auto kInputActionReleaseBase = 0;
    enum Enum : i32{
        Release = kInputActionReleaseBase,
        Press,
        Repeat,
    };
};

namespace InputModifier{
    enum Enum : i32{
        Shift = 0x0001,
        Control = 0x0002,
        Alt = 0x0004,
        Super = 0x0008,
        CapsLock = 0x0010,
        NumLock = 0x0020,
    };
};

namespace MouseButton{
    static constexpr auto kMouseButtonLeftBase = 0;
    enum Enum : i32{
        Left = kMouseButtonLeftBase,
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
    static constexpr i32 kKeyUnknownCode = -1;
    static constexpr i32 kKeySpaceAscii = 32;
    static constexpr i32 kKeyWorld1Code = 161;
    static constexpr i32 kKeyLeftShiftCode = 340;
    static constexpr i32 kKeyKeypad0Code = 320;
    static constexpr i32 kKeyF1Code = 290;
    static constexpr i32 kKeyCapsLockCode = 280;
    static constexpr i32 kKeyEscapeCode = 256;
    static constexpr i32 kKeyGraveAccentCode = 96;
    static constexpr i32 kKeyLeftBracketCode = 91;
    static constexpr i32 kKeyACode = 65;
    static constexpr i32 kKeyEqualCode = 61;
    static constexpr i32 kKeySemicolonCode = 59;
    static constexpr i32 kKeyCommaCode = 44;
    static constexpr i32 kKeyApostropheCode = 39;
    enum Enum : i32{
        Unknown = kKeyUnknownCode,

        Space = kKeySpaceAscii,
        Apostrophe = kKeyApostropheCode,
        Comma = kKeyCommaCode,
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
        Semicolon = kKeySemicolonCode,
        Equal = kKeyEqualCode,
        A = kKeyACode,
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
        LeftBracket = kKeyLeftBracketCode,
        Backslash,
        RightBracket,
        GraveAccent = kKeyGraveAccentCode,
        World2,

        Escape = kKeyEscapeCode,
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
        CapsLock = kKeyCapsLockCode,
        ScrollLock,
        NumLock,
        PrintScreen,
        Pause,
        F1 = kKeyF1Code,
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
        Keypad0 = kKeyKeypad0Code,
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
        LeftShift = kKeyLeftShiftCode,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IInputEventHandler{
public:
    virtual ~IInputEventHandler() = default;


public:
    virtual bool keyboardUpdate(i32, i32, i32, i32){ return false; }
    virtual bool keyboardCharInput(u32, i32){ return false; }
    virtual bool mousePosUpdate(f64, f64){ return false; }
    virtual bool mouseButtonUpdate(i32, i32, i32){ return false; }
    virtual bool mouseScrollUpdate(f64, f64){ return false; }
};


namespace HandlerMutationType{
    enum Enum : u8{
        AddFront,
        AddBack,
        Remove,
    };
};


class InputDispatcher{
private:
    struct HandlerMutation{
        IInputEventHandler* handler = nullptr;
        HandlerMutationType::Enum type = HandlerMutationType::Remove;
    };

    using HandlerList = List<IInputEventHandler*, Alloc::GlobalArena>;
    using HandlerMutationVector = Vector<HandlerMutation, Alloc::GlobalArena>;


public:
    InputDispatcher();


public:
    void addHandlerToFront(IInputEventHandler& handler);
    void addHandlerToBack(IInputEventHandler& handler);
    void removeHandler(IInputEventHandler& handler);

    void setMousePositionScale(f32 x, f32 y);

    void keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods);
    void keyboardCharInput(u32 unicode, i32 mods);
    void mousePosUpdate(f64 xpos, f64 ypos);
    void mouseButtonUpdate(i32 button, i32 action, i32 mods);
    void mouseScrollUpdate(f64 xoffset, f64 yoffset);


private:
    template<typename DispatchFunc>
    void dispatchToHandlers(DispatchFunc&& dispatchFunc){
        ++m_dispatchDepth;

        for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
            IInputEventHandler* handler = *it;
            if(m_pendingHandlerRemovalCount > 0 && isHandlerPendingRemoval(*handler))
                continue;
            if(dispatchFunc(*handler))
                break;
        }

        --m_dispatchDepth;
        if(m_dispatchDepth == 0 && !m_pendingHandlerMutations.empty())
            applyPendingHandlerMutations();
    }

    void queueOrApplyHandlerMutation(HandlerMutationType::Enum type, IInputEventHandler& handler);
    void applyPendingHandlerMutations();
    bool isHandlerPendingRemoval(const IInputEventHandler& handler)const;


private:
    Alloc::GlobalArena m_arena;
    HandlerList m_handlers;
    HandlerMutationVector m_pendingHandlerMutations;
    usize m_pendingHandlerRemovalCount = 0;
    u32 m_dispatchDepth = 0;
    f32 m_mousePositionScaleX = 1.f;
    f32 m_mousePositionScaleY = 1.f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

