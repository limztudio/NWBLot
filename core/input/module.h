// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/alloc/module.h>


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

static_assert(Key::World1 == 161 && Key::World2 == 162);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IInputEventHandler{
public:
    virtual ~IInputEventHandler() = default;


public:
    virtual void windowFocusUpdate(bool){}
    virtual void pointerLeave(){}
    virtual void pointerCaptureLost(){}
    virtual bool keyboardUpdate(i32, i32, i32, i32){ return false; }
    // Queried immediately from the consuming press/repeat handler before native ordinary text is admitted.
    [[nodiscard]] virtual bool blocksKeyboardText()const{ return false; }
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

    void setMousePositionScale(f32 x, f32 y)noexcept;

    [[nodiscard]] bool windowFocused()const noexcept{ return m_windowFocused; }
    // Focus is window lifecycle state; every current handler observes each transition.
    void windowFocusUpdate(bool focused);
    // Native pointer lifecycle reaches every owner and never consumes keyboard state.
    void pointerLeave();
    void pointerCaptureLost();

    void keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods);
    // Releases preserve queued character policy; a supplied native scancode selects its captured press/repeat policy.
    [[nodiscard]] bool keyboardTextBlocked(i32 scancode = -1)const noexcept;
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
    bool isHandlerPendingRemoval(const IInputEventHandler& handler)const noexcept;


private:
    Alloc::GlobalArena m_arena;
    HandlerList m_handlers;
    HandlerMutationVector m_pendingHandlerMutations;
    usize m_pendingHandlerRemovalCount = 0;
    u32 m_dispatchDepth = 0;
    u64 m_keyboardTextPolicyEpoch = 0u;
    Array<bool, 512u> m_keyboardTextPolicies{};
    bool m_keyboardTextBlocked = false;
    bool m_windowFocused = true;
    f32 m_mousePositionScaleX = 1.f;
    f32 m_mousePositionScaleY = 1.f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

