// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_linux_platform.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <logger/client/logger.h>

#include <thread.h>


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

static i32 AdjustModifiersForKey(i32 key, i32 action, i32 mods){
    switch(key){
    case Key::LeftShift:
    case Key::RightShift:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Shift)
            : (mods | InputModifier::Shift);
    case Key::LeftControl:
    case Key::RightControl:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Control)
            : (mods | InputModifier::Control);
    case Key::LeftAlt:
    case Key::RightAlt:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Alt)
            : (mods | InputModifier::Alt);
    case Key::LeftSuper:
    case Key::RightSuper:
        return action == InputAction::Release
            ? (mods & ~InputModifier::Super)
            : (mods | InputModifier::Super);
    default:
        return mods;
    }
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
    case Button4:
        yoffset = 1.0;
        return true;
    case Button5:
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

static i32 TranslateKey(KeySym keySym){
    if(keySym >= XK_0 && keySym <= XK_9)
        return static_cast<i32>(keySym);

    if(keySym >= XK_A && keySym <= XK_Z)
        return static_cast<i32>(keySym);

    if(keySym >= XK_a && keySym <= XK_z)
        return static_cast<i32>(Key::A + (keySym - XK_a));

    if(keySym >= XK_F1 && keySym <= XK_F24)
        return static_cast<i32>(Key::F1 + (keySym - XK_F1));

    if(keySym >= XK_KP_0 && keySym <= XK_KP_9)
        return static_cast<i32>(Key::Keypad0 + (keySym - XK_KP_0));

    if(keySym == XK_ISO_Left_Tab)
        return Key::Tab;
    if(keySym == XK_grave || keySym == XK_quoteleft)
        return Key::GraveAccent;
    if(keySym == XK_Print || keySym == XK_Sys_Req)
        return Key::PrintScreen;
    if(keySym == XK_Alt_L || keySym == XK_Meta_L)
        return Key::LeftAlt;
    if(keySym == XK_Alt_R || keySym == XK_Meta_R)
        return Key::RightAlt;
    if(keySym == XK_Super_L || keySym == XK_Hyper_L)
        return Key::LeftSuper;
    if(keySym == XK_Super_R || keySym == XK_Hyper_R)
        return Key::RightSuper;

    switch(keySym){
    case XK_space: return Key::Space;
    case XK_apostrophe: return Key::Apostrophe;
    case XK_comma: return Key::Comma;
    case XK_minus: return Key::Minus;
    case XK_period: return Key::Period;
    case XK_slash: return Key::Slash;
    case XK_semicolon: return Key::Semicolon;
    case XK_equal: return Key::Equal;
    case XK_bracketleft: return Key::LeftBracket;
    case XK_backslash: return Key::Backslash;
    case XK_bracketright: return Key::RightBracket;

    case XK_Escape: return Key::Escape;
    case XK_Return: return Key::Enter;
    case XK_Tab: return Key::Tab;
    case XK_BackSpace: return Key::Backspace;
    case XK_Insert: return Key::Insert;
    case XK_Delete: return Key::Delete;
    case XK_Right: return Key::Right;
    case XK_Left: return Key::Left;
    case XK_Down: return Key::Down;
    case XK_Up: return Key::Up;
    case XK_Prior: return Key::PageUp;
    case XK_Next: return Key::PageDown;
    case XK_Home: return Key::Home;
    case XK_End: return Key::End;
    case XK_Caps_Lock: return Key::CapsLock;
    case XK_Scroll_Lock: return Key::ScrollLock;
    case XK_Num_Lock: return Key::NumLock;
    case XK_Pause: return Key::Pause;
    case XK_F25: return Key::F25;

    case XK_KP_Insert: return Key::Keypad0;
    case XK_KP_End: return Key::Keypad1;
    case XK_KP_Down: return Key::Keypad2;
    case XK_KP_Next: return Key::Keypad3;
    case XK_KP_Left: return Key::Keypad4;
    case XK_KP_Begin: return Key::Keypad5;
    case XK_KP_Right: return Key::Keypad6;
    case XK_KP_Home: return Key::Keypad7;
    case XK_KP_Up: return Key::Keypad8;
    case XK_KP_Prior: return Key::Keypad9;
    case XK_KP_Delete: return Key::KeypadDecimal;
    case XK_KP_Decimal: return Key::KeypadDecimal;
    case XK_KP_Divide: return Key::KeypadDivide;
    case XK_KP_Multiply: return Key::KeypadMultiply;
    case XK_KP_Subtract: return Key::KeypadSubtract;
    case XK_KP_Add: return Key::KeypadAdd;
    case XK_KP_Enter: return Key::KeypadEnter;
    case XK_KP_Equal: return Key::KeypadEqual;

    case XK_Shift_L: return Key::LeftShift;
    case XK_Control_L: return Key::LeftControl;
    case XK_Shift_R: return Key::RightShift;
    case XK_Control_R: return Key::RightControl;
    case XK_Menu: return Key::Menu;
    default:
        return Key::Unknown;
    }
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

static void DispatchTextInput(IDeviceManager& deviceManager, XKeyEvent keyEvent, i32 mods){
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

        deviceManager.keyboardCharInput(unicode, mods);
    }
}

static void DispatchKeyEvent(Frame& frame, const XKeyEvent& keyEvent, i32 action){
    if(auto* deviceManager = frame.graphics().getDeviceManager()){
        XKeyEvent translatedEvent = keyEvent;
        const KeySym keySym = XLookupKeysym(&translatedEvent, 0);
        const i32 key = TranslateKey(keySym);
        const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers(keyEvent.state));

        deviceManager->keyboardUpdate(key, static_cast<i32>(keyEvent.keycode), action, mods);
        if(action != InputAction::Release)
            DispatchTextInput(*deviceManager, translatedEvent, mods);
    }
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
        if(auto* deviceManager = frame.graphics().getDeviceManager()){
            f64 xoffset = 0.0;
            f64 yoffset = 0.0;
            if(TranslateScroll(event.xbutton.button, xoffset, yoffset)){
                deviceManager->mouseScrollUpdate(xoffset, yoffset);
                break;
            }

            const i32 button = TranslateMouseButton(event.xbutton.button);
            if(button != -1)
                deviceManager->mouseButtonUpdate(button, InputAction::Press, TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case ButtonRelease:
    {
        f64 xoffset = 0.0;
        f64 yoffset = 0.0;
        if(TranslateScroll(event.xbutton.button, xoffset, yoffset))
            break;

        if(auto* deviceManager = frame.graphics().getDeviceManager()){
            const i32 button = TranslateMouseButton(event.xbutton.button);
            if(button != -1)
                deviceManager->mouseButtonUpdate(button, InputAction::Release, TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case MotionNotify:
        if(auto* deviceManager = frame.graphics().getDeviceManager())
            deviceManager->mousePosUpdate(static_cast<f64>(event.xmotion.x), static_cast<f64>(event.xmotion.y));
        break;

    case EnterNotify:
        if(auto* deviceManager = frame.graphics().getDeviceManager())
            deviceManager->mousePosUpdate(static_cast<f64>(event.xcrossing.x), static_cast<f64>(event.xcrossing.y));
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

        frame.graphics().updateWindowState(width, height, windowVisible, windowIsInFocus);
        if(auto* deviceManager = frame.graphics().getDeviceManager()){
            const tchar* title = deviceManager->getWindowTitle();
            if(title && frame.appliedWindowTitle() != title){
                frame.appliedWindowTitle() = title;
                XStoreName(GetX11Display(frameData), GetX11Window(frameData), frame.appliedWindowTitle().c_str());
                XFlush(GetX11Display(frameData));
            }
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

