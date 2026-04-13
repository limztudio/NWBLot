// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_linux_platform.h"
#include "frame_input_helpers.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#ifdef Button4
#undef Button4
#endif
#ifdef Button5
#undef Button5
#endif

#include <logger/client/logger.h>

#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_KeyStateCount = 256;

static bool s_DetectableAutoRepeat = false;
static bool s_KeyStates[s_KeyStateCount] = {};


static ::Display* GetX11Display(const Common::LinuxFrame& frameData){
    return reinterpret_cast<::Display*>(frameData.nativeDisplay());
}

static void SetX11Display(Common::LinuxFrame& frameData, ::Display* display){
    frameData.nativeDisplay() = display;
}

static ::Window GetX11Window(const Common::LinuxFrame& frameData){
    return static_cast<::Window>(frameData.nativeWindowHandle());
}

static void SetX11Window(Common::LinuxFrame& frameData, ::Window window){
    frameData.nativeWindowHandle() = static_cast<u64>(window);
}

static Atom GetDeleteWindowMessage(const Common::LinuxFrame& frameData){
    return static_cast<Atom>(frameData.nativeAuxValue());
}

static void SetDeleteWindowMessage(Common::LinuxFrame& frameData, Atom atom){
    frameData.nativeAuxValue() = static_cast<u64>(atom);
}

static void ResetKeyStates(){
    NWB_MEMSET(s_KeyStates, 0, sizeof(s_KeyStates));
}

static i32 TranslateModifiers(u32 state){
    i32 mods = 0;

    if(state & ShiftMask)
        mods |= InputModifier::Shift;
    if(state & ControlMask)
        mods |= InputModifier::Control;
    if(state & Mod1Mask)
        mods |= InputModifier::Alt;
    if(state & Mod4Mask)
        mods |= InputModifier::Super;
    if(state & LockMask)
        mods |= InputModifier::CapsLock;
    if(state & Mod2Mask)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslateMouseButton(u32 button){
    switch(button){
    case Button1: return MouseButton::Left;
    case Button2: return MouseButton::Middle;
    case Button3: return MouseButton::Right;
    case 8: return MouseButton::Button4;
    case 9: return MouseButton::Button5;
    case 10: return MouseButton::Button6;
    case 11: return MouseButton::Button7;
    case 12: return MouseButton::Button8;
    default:
        return -1;
    }
}

static bool TranslateScroll(u32 button, f64& xoffset, f64& yoffset){
    xoffset = 0.0;
    yoffset = 0.0;

    switch(button){
    case 4:
        yoffset = 1.0;
        return true;
    case 5:
        yoffset = -1.0;
        return true;
    case 6:
        xoffset = -1.0;
        return true;
    case 7:
        xoffset = 1.0;
        return true;
    default:
        return false;
    }
}

struct X11KeySymbols{
    using value_type = KeySym;

    static constexpr value_type Key0 = XK_0;
    static constexpr value_type Key9 = XK_9;
    static constexpr value_type KeyA = XK_A;
    static constexpr value_type KeyZ = XK_Z;
    static constexpr value_type Keya = XK_a;
    static constexpr value_type Keyz = XK_z;
    static constexpr value_type F1 = XK_F1;
    static constexpr value_type F24 = XK_F24;
    static constexpr value_type KP0 = XK_KP_0;
    static constexpr value_type KP9 = XK_KP_9;
    static constexpr value_type ISOLeftTab = XK_ISO_Left_Tab;
    static constexpr value_type Grave = XK_grave;
    static constexpr value_type QuoteLeft = XK_quoteleft;
    static constexpr value_type Print = XK_Print;
    static constexpr value_type SysReq = XK_Sys_Req;
    static constexpr value_type AltL = XK_Alt_L;
    static constexpr value_type MetaL = XK_Meta_L;
    static constexpr value_type AltR = XK_Alt_R;
    static constexpr value_type MetaR = XK_Meta_R;
    static constexpr value_type SuperL = XK_Super_L;
    static constexpr value_type HyperL = XK_Hyper_L;
    static constexpr value_type SuperR = XK_Super_R;
    static constexpr value_type HyperR = XK_Hyper_R;
    static constexpr value_type Space = XK_space;
    static constexpr value_type Apostrophe = XK_apostrophe;
    static constexpr value_type Comma = XK_comma;
    static constexpr value_type Minus = XK_minus;
    static constexpr value_type Period = XK_period;
    static constexpr value_type Slash = XK_slash;
    static constexpr value_type Semicolon = XK_semicolon;
    static constexpr value_type Equal = XK_equal;
    static constexpr value_type BracketLeft = XK_bracketleft;
    static constexpr value_type Backslash = XK_backslash;
    static constexpr value_type BracketRight = XK_bracketright;
    static constexpr value_type Escape = XK_Escape;
    static constexpr value_type ReturnKey = XK_Return;
    static constexpr value_type Tab = XK_Tab;
    static constexpr value_type BackSpace = XK_BackSpace;
    static constexpr value_type Insert = XK_Insert;
    static constexpr value_type DeleteKey = XK_Delete;
    static constexpr value_type Right = XK_Right;
    static constexpr value_type Left = XK_Left;
    static constexpr value_type Down = XK_Down;
    static constexpr value_type Up = XK_Up;
    static constexpr value_type Prior = XK_Prior;
    static constexpr value_type Next = XK_Next;
    static constexpr value_type Home = XK_Home;
    static constexpr value_type End = XK_End;
    static constexpr value_type CapsLock = XK_Caps_Lock;
    static constexpr value_type ScrollLock = XK_Scroll_Lock;
    static constexpr value_type NumLock = XK_Num_Lock;
    static constexpr value_type Pause = XK_Pause;
    static constexpr value_type F25 = XK_F25;
    static constexpr value_type KPInsert = XK_KP_Insert;
    static constexpr value_type KPEnd = XK_KP_End;
    static constexpr value_type KPDown = XK_KP_Down;
    static constexpr value_type KPNext = XK_KP_Next;
    static constexpr value_type KPLeft = XK_KP_Left;
    static constexpr value_type KPBegin = XK_KP_Begin;
    static constexpr value_type KPRight = XK_KP_Right;
    static constexpr value_type KPHome = XK_KP_Home;
    static constexpr value_type KPUp = XK_KP_Up;
    static constexpr value_type KPPrior = XK_KP_Prior;
    static constexpr value_type KPDelete = XK_KP_Delete;
    static constexpr value_type KPDecimal = XK_KP_Decimal;
    static constexpr value_type KPDivide = XK_KP_Divide;
    static constexpr value_type KPMultiply = XK_KP_Multiply;
    static constexpr value_type KPSubtract = XK_KP_Subtract;
    static constexpr value_type KPAdd = XK_KP_Add;
    static constexpr value_type KPEnter = XK_KP_Enter;
    static constexpr value_type KPEqual = XK_KP_Equal;
    static constexpr value_type ShiftL = XK_Shift_L;
    static constexpr value_type ControlL = XK_Control_L;
    static constexpr value_type ShiftR = XK_Shift_R;
    static constexpr value_type ControlR = XK_Control_R;
    static constexpr value_type Menu = XK_Menu;
};

static i32 TranslateKey(KeySym keySym){
    return TranslateLinuxKeySymbol<X11KeySymbols>(keySym);
}

static bool IsUtf8Continuation(u8 value){
    return (value & 0xc0u) == 0x80u;
}

static i32 DecodeUtf8CodePoint(const char* bytes, i32 length, u32& unicode){
    if(length <= 0)
        return 0;

    const u8 c0 = static_cast<u8>(bytes[0]);
    if(c0 < 0x80u){
        unicode = c0;
        return 1;
    }

    if((c0 & 0xe0u) == 0xc0u){
        if(length < 2)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        if(!IsUtf8Continuation(c1))
            return 0;

        unicode = (static_cast<u32>(c0 & 0x1fu) << 6) | static_cast<u32>(c1 & 0x3fu);
        return unicode >= 0x80u ? 2 : 0;
    }

    if((c0 & 0xf0u) == 0xe0u){
        if(length < 3)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2))
            return 0;

        unicode =
            (static_cast<u32>(c0 & 0x0fu) << 12)
            | (static_cast<u32>(c1 & 0x3fu) << 6)
            | static_cast<u32>(c2 & 0x3fu)
            ;
        return unicode >= 0x800u ? 3 : 0;
    }

    if((c0 & 0xf8u) == 0xf0u){
        if(length < 4)
            return 0;

        const u8 c1 = static_cast<u8>(bytes[1]);
        const u8 c2 = static_cast<u8>(bytes[2]);
        const u8 c3 = static_cast<u8>(bytes[3]);
        if(!IsUtf8Continuation(c1) || !IsUtf8Continuation(c2) || !IsUtf8Continuation(c3))
            return 0;

        unicode =
            (static_cast<u32>(c0 & 0x07u) << 18)
            | (static_cast<u32>(c1 & 0x3fu) << 12)
            | (static_cast<u32>(c2 & 0x3fu) << 6)
            | static_cast<u32>(c3 & 0x3fu)
            ;
        return unicode >= 0x10000u && unicode <= 0x10ffffu ? 4 : 0;
    }

    return 0;
}

static void DispatchTextInput(InputDispatcher& input, XKeyEvent keyEvent, i32 mods){
    char buffer[64] = {};
    KeySym ignored = NoSymbol;
    const i32 byteCount = XLookupString(&keyEvent, buffer, static_cast<i32>(sizeof(buffer)), &ignored, nullptr);
    if(byteCount <= 0)
        return;

    for(i32 i = 0; i < byteCount;){
        u32 unicode = 0;
        i32 consumed = DecodeUtf8CodePoint(buffer + i, byteCount - i, unicode);
        if(consumed <= 0){
            unicode = static_cast<u8>(buffer[i]);
            consumed = 1;
        }

        i += consumed;

        if(unicode < 32 || unicode == 127)
            continue;

        input.keyboardCharInput(unicode, mods);
    }
}

static void DispatchKeyEvent(Frame& frame, const XKeyEvent& keyEvent, i32 action){
    XKeyEvent translatedEvent = keyEvent;
    const KeySym keySym = XLookupKeysym(&translatedEvent, 0);
    const i32 key = TranslateKey(keySym);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers(keyEvent.state));

    frame.input().keyboardUpdate(key, static_cast<i32>(keyEvent.keycode), action, mods);
    if(action != InputAction::Release)
        DispatchTextInput(frame.input(), translatedEvent, mods);
}

static bool ProcessEvent(Frame& frame, const XEvent& event){
    auto& frameData = frame.data<Common::LinuxFrame>();

    switch(event.type){
    case ClientMessage:
    {
        const Atom message = static_cast<Atom>(event.xclient.data.l[0]);
        if(message == GetDeleteWindowMessage(frameData))
            return false;
    }
    break;

    case DestroyNotify:
        SetX11Window(frameData, 0);
        return false;

    case FocusIn:
        frameData.isActive() = true;
        break;

    case FocusOut:
        frameData.isActive() = false;
        ResetKeyStates();
        break;

    case KeyPress:
    {
        const u32 keycode = static_cast<u32>(event.xkey.keycode);
        i32 action = InputAction::Press;
        if(keycode < s_KeyStateCount){
            action = s_KeyStates[keycode] ? InputAction::Repeat : InputAction::Press;
            s_KeyStates[keycode] = true;
        }

        DispatchKeyEvent(frame, event.xkey, action);
    }
    break;

    case KeyRelease:
    {
        if(!s_DetectableAutoRepeat && XEventsQueued(GetX11Display(frameData), QueuedAfterReading) > 0){
            XEvent nextEvent = {};
            XPeekEvent(GetX11Display(frameData), &nextEvent);
            if(nextEvent.type == KeyPress
                && nextEvent.xkey.keycode == event.xkey.keycode
                && nextEvent.xkey.time == event.xkey.time)
                break;
        }

        const u32 keycode = static_cast<u32>(event.xkey.keycode);
        if(keycode < s_KeyStateCount)
            s_KeyStates[keycode] = false;

        DispatchKeyEvent(frame, event.xkey, InputAction::Release);
    }
    break;

    case ButtonPress:
    {
        f64 xoffset = 0.0;
        f64 yoffset = 0.0;
        if(TranslateScroll(event.xbutton.button, xoffset, yoffset)){
            frame.input().mouseScrollUpdate(xoffset, yoffset);
            break;
        }

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1)
            frame.input().mouseButtonUpdate(button, InputAction::Press, TranslateModifiers(event.xbutton.state));
    }
    break;

    case ButtonRelease:
    {
        f64 xoffset = 0.0;
        f64 yoffset = 0.0;
        if(TranslateScroll(event.xbutton.button, xoffset, yoffset))
            break;

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1)
            frame.input().mouseButtonUpdate(button, InputAction::Release, TranslateModifiers(event.xbutton.state));
    }
    break;

    case MotionNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xmotion.x), static_cast<f64>(event.xmotion.y));
        break;

    case EnterNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xcrossing.x), static_cast<f64>(event.xcrossing.y));
        break;

    case MappingNotify:
    {
        XMappingEvent mappingEvent = event.xmapping;
        XRefreshKeyboardMapping(&mappingEvent);
    }
    break;
    }

    return true;
}

static bool QueryWindowState(Frame& frame, u32& width, u32& height, bool& windowVisible, bool& windowIsInFocus){
    auto& frameData = frame.data<Common::LinuxFrame>();

    XWindowAttributes attributes = {};
    if(XGetWindowAttributes(GetX11Display(frameData), GetX11Window(frameData), &attributes) == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame window attribute query failed"));
        return false;
    }

    windowVisible = attributes.map_state == IsViewable && attributes.width > 0 && attributes.height > 0;
    width = windowVisible ? static_cast<u32>(attributes.width) : 0;
    height = windowVisible ? static_cast<u32>(attributes.height) : 0;
    windowIsInFocus = frameData.isActive();

    return true;
}

static void ResetFrameData(Common::LinuxFrame& frameData){
    frameData.isActive() = false;
    SetX11Display(frameData, nullptr);
    SetX11Window(frameData, 0);
    SetDeleteWindowMessage(frameData, None);
    s_DetectableAutoRepeat = false;
    ResetKeyStates();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InitX11Frame(Frame& frame){
    constexpr const tchar* AppName = NWB_TEXT("NWBLoader");
    constexpr long EventMask =
        ExposureMask
        | FocusChangeMask
        | StructureNotifyMask
        | KeyPressMask
        | KeyReleaseMask
        | ButtonPressMask
        | ButtonReleaseMask
        | PointerMotionMask
        | EnterWindowMask
        | LeaveWindowMask
        ;

    auto& frameData = frame.data<Common::LinuxFrame>();
    ResetFrameData(frameData);

    SetX11Display(frameData, XOpenDisplay(nullptr));
    if(!GetX11Display(frameData)){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 display connection failed"));
        return false;
    }

    const i32 screen = DefaultScreen(GetX11Display(frameData));
    const i32 displayWidth = DisplayWidth(GetX11Display(frameData), screen);
    const i32 displayHeight = DisplayHeight(GetX11Display(frameData), screen);
    const i32 centeredX = (displayWidth - static_cast<i32>(frameData.width())) >> 1;
    const i32 centeredY = (displayHeight - static_cast<i32>(frameData.height())) >> 1;
    const i32 x = centeredX > 0 ? centeredX : 0;
    const i32 y = centeredY > 0 ? centeredY : 0;

    SetX11Window(frameData, XCreateSimpleWindow(
        GetX11Display(frameData),
        RootWindow(GetX11Display(frameData), screen),
        x,
        y,
        frameData.width(),
        frameData.height(),
        0,
        BlackPixel(GetX11Display(frameData), screen),
        WhitePixel(GetX11Display(frameData), screen)
    ));
    if(GetX11Window(frameData) == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 window creation failed"));
        CleanupX11Frame(frame);
        return false;
    }

    Bool detectableAutoRepeat = False;
    s_DetectableAutoRepeat =
        XkbSetDetectableAutoRepeat(GetX11Display(frameData), True, &detectableAutoRepeat) != False
        && detectableAutoRepeat != False
        ;

    XSelectInput(GetX11Display(frameData), GetX11Window(frameData), EventMask);
    XStoreName(GetX11Display(frameData), GetX11Window(frameData), AppName);

    SetDeleteWindowMessage(frameData, XInternAtom(GetX11Display(frameData), "WM_DELETE_WINDOW", False));
    if(GetDeleteWindowMessage(frameData) == None){
        CleanupX11Frame(frame);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 window close protocol registration failed"));
        return false;
    }
    Atom deleteWindowMessage = GetDeleteWindowMessage(frameData);
    if(XSetWMProtocols(GetX11Display(frameData), GetX11Window(frameData), &deleteWindowMessage, 1) == 0){
        CleanupX11Frame(frame);
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 window close protocol installation failed"));
        return false;
    }

    return true;
}

bool ShowX11Frame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();

    XMapRaised(GetX11Display(frameData), GetX11Window(frameData));
    XFlush(GetX11Display(frameData));
    return true;
}

bool RunX11Frame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();

    Timer lateTime(TimerNow());

    for(;;){
        while(XPending(GetX11Display(frameData)) > 0){
            XEvent event = {};
            XNextEvent(GetX11Display(frameData), &event);
            if(!ProcessEvent(frame, event))
                return true;
        }

        u32 width = 0;
        u32 height = 0;
        bool windowVisible = false;
        bool windowIsInFocus = false;
        if(!QueryWindowState(frame, width, height, windowVisible, windowIsInFocus))
            return false;

        if(const tchar* title = frame.syncGraphicsWindowState(width, height, windowVisible, windowIsInFocus)){
            XStoreName(GetX11Display(frameData), GetX11Window(frameData), title);
            XFlush(GetX11Display(frameData));
        }

        Timer currentTime(TimerNow());
        const f32 delta = DurationInSeconds<f32>(currentTime, lateTime);
        lateTime = currentTime;

        if(!frame.update(delta))
            break;

        if(!windowVisible || !windowIsInFocus)
            SleepMS(10);
    }

    return false;
}

void CleanupX11Frame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();

    if(GetX11Display(frameData)){
        if(GetX11Window(frameData)){
            XDestroyWindow(GetX11Display(frameData), GetX11Window(frameData));
            SetX11Window(frameData, 0);
        }

        XCloseDisplay(GetX11Display(frameData));
        SetX11Display(frameData, nullptr);
    }

    SetDeleteWindowMessage(frameData, None);
    s_DetectableAutoRepeat = false;
    ResetKeyStates();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

