// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputAction{
    enum Enum : i32{
        Release = 0,
        Press = 1,
        Repeat = 2,
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
    enum Enum : i32{
        Left = 0,
        Right = 1,
        Middle = 2,
        Button4 = 3,
        Button5 = 4,
        Button6 = 5,
        Button7 = 6,
        Button8 = 7,
    };
};

namespace Key{
    enum Enum : i32{
        Unknown = -1,

        Space = 32,
        Apostrophe = 39,
        Comma = 44,
        Minus = 45,
        Period = 46,
        Slash = 47,
        Number0 = 48,
        Number1 = 49,
        Number2 = 50,
        Number3 = 51,
        Number4 = 52,
        Number5 = 53,
        Number6 = 54,
        Number7 = 55,
        Number8 = 56,
        Number9 = 57,
        Semicolon = 59,
        Equal = 61,
        A = 65,
        B = 66,
        C = 67,
        D = 68,
        E = 69,
        F = 70,
        G = 71,
        H = 72,
        I = 73,
        J = 74,
        K = 75,
        L = 76,
        M = 77,
        N = 78,
        O = 79,
        P = 80,
        Q = 81,
        R = 82,
        S = 83,
        T = 84,
        U = 85,
        V = 86,
        W = 87,
        X = 88,
        Y = 89,
        Z = 90,
        LeftBracket = 91,
        Backslash = 92,
        RightBracket = 93,
        GraveAccent = 96,

        World1 = 161,
        World2 = 162,

        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,
        Insert = 260,
        Delete = 261,
        Right = 262,
        Left = 263,
        Down = 264,
        Up = 265,
        PageUp = 266,
        PageDown = 267,
        Home = 268,
        End = 269,
        CapsLock = 280,
        ScrollLock = 281,
        NumLock = 282,
        PrintScreen = 283,
        Pause = 284,
        F1 = 290,
        F2 = 291,
        F3 = 292,
        F4 = 293,
        F5 = 294,
        F6 = 295,
        F7 = 296,
        F8 = 297,
        F9 = 298,
        F10 = 299,
        F11 = 300,
        F12 = 301,
        F13 = 302,
        F14 = 303,
        F15 = 304,
        F16 = 305,
        F17 = 306,
        F18 = 307,
        F19 = 308,
        F20 = 309,
        F21 = 310,
        F22 = 311,
        F23 = 312,
        F24 = 313,
        F25 = 314,
        Keypad0 = 320,
        Keypad1 = 321,
        Keypad2 = 322,
        Keypad3 = 323,
        Keypad4 = 324,
        Keypad5 = 325,
        Keypad6 = 326,
        Keypad7 = 327,
        Keypad8 = 328,
        Keypad9 = 329,
        KeypadDecimal = 330,
        KeypadDivide = 331,
        KeypadMultiply = 332,
        KeypadSubtract = 333,
        KeypadAdd = 334,
        KeypadEnter = 335,
        KeypadEqual = 336,
        LeftShift = 340,
        LeftControl = 341,
        LeftAlt = 342,
        LeftSuper = 343,
        RightShift = 344,
        RightControl = 345,
        RightAlt = 346,
        RightSuper = 347,
        Menu = 348,
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
    struct HandlerMutation{
        HandlerMutationType::Enum type = HandlerMutationType::Remove;
        IInputEventHandler* handler = nullptr;
    };

    template<typename DispatchFunc>
    void dispatchToHandlers(DispatchFunc&& dispatchFunc){
        ++m_dispatchDepth;

        for(auto it = m_handlers.crbegin(); it != m_handlers.crend(); ++it){
            IInputEventHandler* handler = *it;
            if(!m_pendingHandlerMutations.empty() && isHandlerPendingRemoval(*handler))
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
    List<IInputEventHandler*> m_handlers;
    Vector<HandlerMutation> m_pendingHandlerMutations;
    u32 m_dispatchDepth = 0;
    f32 m_mousePositionScaleX = 1.f;
    f32 m_mousePositionScaleY = 1.f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

