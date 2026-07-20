#include "linux_platform.h"
#include "input_helpers.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#ifdef Button4
#undef Button4
#endif
#ifdef Button5
#undef Button5
#endif

#include <core/common/log.h>

#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_KeyStateCount = 256;
static constexpr u32 s_X11ScrollUpButton = 4u;
static constexpr u32 s_X11ScrollDownButton = 5u;
static constexpr u32 s_X11ScrollLeftButton = 6u;
static constexpr u32 s_X11ScrollRightButton = 7u;
static constexpr u32 s_X11MouseButton4 = 8u;
static constexpr u32 s_X11MouseButton5 = 9u;
static constexpr u32 s_X11MouseButton6 = 10u;
static constexpr u32 s_X11MouseButton7 = 11u;
static constexpr u32 s_X11MouseButton8 = 12u;
static constexpr f64 s_ScrollUnit = 1.0;
static constexpr usize s_TextInputBufferSize = 64u;
static constexpr u32 s_TextInputControlCodePointLimit = 32u;
static constexpr u32 s_TextInputDeleteCodePoint = 127u;

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

static u16 ClampInitialWindowDimension(const u16 requestedDimension, const i32 displayDimension){
    if(displayDimension <= 0)
        return requestedDimension;

    const u32 clampedDimension = Min<u32>(static_cast<u32>(requestedDimension), static_cast<u32>(displayDimension));
    return static_cast<u16>(Min<u32>(clampedDimension, s_MaxU16));
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
    case s_X11MouseButton4: return MouseButton::Button4;
    case s_X11MouseButton5: return MouseButton::Button5;
    case s_X11MouseButton6: return MouseButton::Button6;
    case s_X11MouseButton7: return MouseButton::Button7;
    case s_X11MouseButton8: return MouseButton::Button8;
    default:
        return -1;
    }
}

static bool TranslateScroll(u32 button, f64& xoffset, f64& yoffset){
    xoffset = 0.0;
    yoffset = 0.0;

    switch(button){
    case s_X11ScrollUpButton:
        yoffset = s_ScrollUnit;
        return true;
    case s_X11ScrollDownButton:
        yoffset = -s_ScrollUnit;
        return true;
    case s_X11ScrollLeftButton:
        xoffset = -s_ScrollUnit;
        return true;
    case s_X11ScrollRightButton:
        xoffset = s_ScrollUnit;
        return true;
    default:
        return false;
    }
}

NWB_DECLARE_LINUX_KEY_SYMBOLS(X11KeySymbols, KeySym, XK);

static i32 TranslateKey(KeySym keySym){
    return TranslateLinuxKeySymbol<X11KeySymbols>(keySym);
}

static void DispatchTextInput(InputDispatcher& input, XKeyEvent keyEvent, i32 mods){
    char buffer[s_TextInputBufferSize] = {};
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

        if(unicode < s_TextInputControlCodePointLimit || unicode == s_TextInputDeleteCodePoint)
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
    case ClientMessage: {
        const Atom message = static_cast<Atom>(event.xclient.data.l[0]);
        if(message == GetDeleteWindowMessage(frameData))
            return false;
    }
    break;

    case DestroyNotify:
        SetX11Window(frameData, 0);
        return false;

    case FocusIn:
        frameData.setActive(true);
        break;

    case FocusOut:
        frameData.setActive(false);
        ResetKeyStates();
        break;

    case KeyPress: {
        const u32 keycode = static_cast<u32>(event.xkey.keycode);
        i32 action = InputAction::Press;
        if(keycode < s_KeyStateCount){
            action = s_KeyStates[keycode] ? InputAction::Repeat : InputAction::Press;
            s_KeyStates[keycode] = true;
        }

        DispatchKeyEvent(frame, event.xkey, action);
    }
    break;

    case KeyRelease: {
        if(!s_DetectableAutoRepeat && XEventsQueued(GetX11Display(frameData), QueuedAfterReading) > 0){
            XEvent nextEvent = {};
            XPeekEvent(GetX11Display(frameData), &nextEvent);
            if(
                nextEvent.type == KeyPress
                && nextEvent.xkey.keycode == event.xkey.keycode
                && nextEvent.xkey.time == event.xkey.time
            )
                break;
        }

        const u32 keycode = static_cast<u32>(event.xkey.keycode);
        if(keycode < s_KeyStateCount)
            s_KeyStates[keycode] = false;

        DispatchKeyEvent(frame, event.xkey, InputAction::Release);
    }
    break;

    case ButtonPress: {
        f64 xoffset = 0.0;
        f64 yoffset = 0.0;
        if(TranslateScroll(event.xbutton.button, xoffset, yoffset)){
            frame.input().mouseScrollUpdate(xoffset, yoffset);
            break;
        }

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1){
            frame.input().mousePosUpdate(static_cast<f64>(event.xbutton.x), static_cast<f64>(event.xbutton.y));
            frame.input().mouseButtonUpdate(button, InputAction::Press, TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case ButtonRelease: {
        f64 xoffset = 0.0;
        f64 yoffset = 0.0;
        if(TranslateScroll(event.xbutton.button, xoffset, yoffset))
            break;

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1){
            frame.input().mousePosUpdate(static_cast<f64>(event.xbutton.x), static_cast<f64>(event.xbutton.y));
            frame.input().mouseButtonUpdate(button, InputAction::Release, TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case MotionNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xmotion.x), static_cast<f64>(event.xmotion.y));
        break;

    case EnterNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xcrossing.x), static_cast<f64>(event.xcrossing.y));
        break;

    case MappingNotify: {
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
    frameData.setActive(false);
    SetX11Display(frameData, nullptr);
    SetX11Window(frameData, 0);
    SetDeleteWindowMessage(frameData, None);
    s_DetectableAutoRepeat = false;
    ResetKeyStates();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InitX11Frame(Frame& frame){
    const tchar* AppName = frame.windowTitleOrDefault();
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
    const u16 requestedWidth = frameData.width();
    const u16 requestedHeight = frameData.height();
    const u16 windowWidth = ClampInitialWindowDimension(requestedWidth, displayWidth);
    const u16 windowHeight = ClampInitialWindowDimension(requestedHeight, displayHeight);
    if(windowWidth == 0u || windowHeight == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 display dimensions are invalid: {}x{}"), displayWidth, displayHeight);
        CleanupX11Frame(frame);
        return false;
    }
    if(windowWidth != requestedWidth || windowHeight != requestedHeight){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("Frame X11 client size clamped from {}x{} to {}x{} for display {}x{}"),
            requestedWidth,
            requestedHeight,
            windowWidth,
            windowHeight,
            displayWidth,
            displayHeight
        );
    }
    frameData.width() = windowWidth;
    frameData.height() = windowHeight;

    const i32 centeredX = (displayWidth - static_cast<i32>(windowWidth)) >> 1;
    const i32 centeredY = (displayHeight - static_cast<i32>(windowHeight)) >> 1;
    const i32 x = centeredX > 0 ? centeredX : 0;
    const i32 y = centeredY > 0 ? centeredY : 0;

    SetX11Window(frameData, XCreateSimpleWindow(
        GetX11Display(frameData),
        RootWindow(GetX11Display(frameData), screen),
        x,
        y,
        windowWidth,
        windowHeight,
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
    s_DetectableAutoRepeat = XkbSetDetectableAutoRepeat(GetX11Display(frameData), True, &detectableAutoRepeat) != False && detectableAutoRepeat != False;

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
        if(frame.quitRequested())
            return true;

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
            return false;
        if(frame.quitRequested())
            return true;

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

