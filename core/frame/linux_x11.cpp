// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_platform.h"
#include "frame_detail.h"
#include "linux_x11_keyboard.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#if defined(FOUNDATION_PLATFORM_LINUX)
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef Button4
#undef Button4
#endif
#ifdef Button5
#undef Button5
#endif

#include <core/common/log.h>
#include <core/os/linux/x11/clipboard.h>
#include <core/os/linux/x11/text_input.h>

#include <global/scope_exit.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
// Idle throttle while hidden or unfocused; mirrors the Wayland inactive timeout.
static constexpr i32 s_InactivePollSleepMs = 10;
static constexpr int s_X11CardinalPropertyFormatBits = 32;

static X11KeyboardInput s_Keyboard;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void SetX11Display(Common::LinuxFrame& frameData, ::Display* display)noexcept{
    frameData.nativeDisplay() = display;
}

static ::Window GetX11Window(const Common::LinuxFrame& frameData)noexcept{
    return static_cast<::Window>(frameData.nativeWindowHandle());
}

static void SetX11Window(Common::LinuxFrame& frameData, ::Window window)noexcept{
    frameData.nativeWindowHandle() = static_cast<u64>(window);
}

static Atom GetDeleteWindowMessage(const Common::LinuxFrame& frameData)noexcept{
    return static_cast<Atom>(frameData.nativeAuxValue());
}

static void SetDeleteWindowMessage(Common::LinuxFrame& frameData, Atom atom)noexcept{
    frameData.nativeAuxValue() = static_cast<u64>(atom);
}

static i32 TranslateMouseButton(u32 button)noexcept{
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

struct ScrollOffset{
    f64 x = 0.0;
    f64 y = 0.0;
};

[[nodiscard]] static Optional<ScrollOffset> TranslateScroll(const u32 button)noexcept{
    switch(button){
    case s_X11ScrollUpButton: return ScrollOffset{ .y = s_ScrollUnit, };
    case s_X11ScrollDownButton: return ScrollOffset{ .y = -s_ScrollUnit, };
    case s_X11ScrollLeftButton: return ScrollOffset{ .x = -s_ScrollUnit, };
    case s_X11ScrollRightButton: return ScrollOffset{ .x = s_ScrollUnit, };
    default:
        return {};
    }
}

static b8 ProcessEvent(Frame& frame, const XEvent& event){
    auto& frameData = frame.data<Common::LinuxFrame>();
    if(event.xany.window != GetX11Window(frameData))
        return true;

    switch(event.type){
    case ClientMessage: {
        const Atom message = static_cast<Atom>(event.xclient.data.l[0]);
        if(message == GetDeleteWindowMessage(frameData))
            return false;
    }
    break;

    case DestroyNotify:
        if(ITextInputService* const textInput = frame.tryTextInput()){
            if(!textInput->setFocused(false))
                FOUNDATION_FATAL_ASSERT(false);
        }
        frame.input().pointerCaptureLost();
        frame.input().pointerLeave();
        SetX11Window(frameData, 0);
        return false;

    case UnmapNotify:
        if(ITextInputService* const textInput = frame.tryTextInput()){
            if(!textInput->setFocused(false))
                FOUNDATION_FATAL_ASSERT(false);
        }
        frame.input().pointerCaptureLost();
        frame.input().pointerLeave();
        break;

    case FocusIn:
        s_Keyboard.synchronize(static_cast<::Display*>(frameData.nativeDisplay()));
        if(ITextInputService* const textInput = frame.tryTextInput()){
            if(!textInput->setFocused(true))
                FOUNDATION_FATAL_ASSERT(false);
        }
        frameData.setActive(true);
        frame.input().windowFocusUpdate(true);
        break;

    case FocusOut:
        if(ITextInputService* const textInput = frame.tryTextInput()){
            if(!textInput->setFocused(false))
                FOUNDATION_FATAL_ASSERT(false);
        }
        frameData.setActive(false);
        s_Keyboard.focusLost();
        frame.input().windowFocusUpdate(false);
        break;

    case KeyPress:
    case KeyRelease:
        s_Keyboard.process(frame, event.xkey, true);
        break;

    case ButtonPress: {
        if(const auto scroll = TranslateScroll(event.xbutton.button)){
            frame.input().mousePosUpdate(static_cast<f64>(event.xbutton.x), static_cast<f64>(event.xbutton.y));
            frame.input().mouseScrollUpdate(scroll->x, scroll->y);
            break;
        }

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1){
            frame.input().mousePosUpdate(static_cast<f64>(event.xbutton.x), static_cast<f64>(event.xbutton.y));
            frame.input().mouseButtonUpdate(button, InputAction::Press, X11KeyboardInput::TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case ButtonRelease: {
        if(TranslateScroll(event.xbutton.button))
            break;

        const i32 button = TranslateMouseButton(event.xbutton.button);
        if(button != -1){
            frame.input().mousePosUpdate(static_cast<f64>(event.xbutton.x), static_cast<f64>(event.xbutton.y));
            frame.input().mouseButtonUpdate(button, InputAction::Release, X11KeyboardInput::TranslateModifiers(event.xbutton.state));
        }
    }
    break;

    case MotionNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xmotion.x), static_cast<f64>(event.xmotion.y));
        break;

    case EnterNotify:
        frame.input().mousePosUpdate(static_cast<f64>(event.xcrossing.x), static_cast<f64>(event.xcrossing.y));
        break;

    case LeaveNotify:
        frame.input().pointerLeave();
        break;

    case MappingNotify: {
        XMappingEvent mappingEvent = event.xmapping;
        XRefreshKeyboardMapping(&mappingEvent);
    }
    break;
    }

    return true;
}

struct X11WindowState final{
    u32 width;
    u32 height;
    b8 windowVisible;
    b8 windowIsInFocus;
};

static Expected<X11WindowState> QueryWindowState(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();

    XWindowAttributes attributes = {};
    if(XGetWindowAttributes(static_cast<::Display*>(frameData.nativeDisplay()), GetX11Window(frameData), &attributes) == 0){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame window attribute query failed"));
        return MakeUnexpected(Failure{});
    }

    const b8 windowVisible = attributes.map_state == IsViewable && attributes.width > 0 && attributes.height > 0;
    return X11WindowState{ .width = windowVisible ? static_cast<u32>(attributes.width) : 0u, .height = windowVisible ? static_cast<u32>(attributes.height) : 0u, .windowVisible = windowVisible, .windowIsInFocus = frameData.isActive() };
}

static void ResetFrameData(Common::LinuxFrame& frameData)noexcept{
    frameData.setActive(false);
    SetX11Display(frameData, nullptr);
    SetX11Window(frameData, 0);
    SetDeleteWindowMessage(frameData, None);
    s_Keyboard.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 InitX11Frame(Frame& frame){
    const TStringView windowTitle = frame.windowTitleOrDefault();
    constexpr long s_WindowEventMask = ExposureMask | FocusChangeMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask;

    auto& frameData = frame.data<Common::LinuxFrame>();
    ResetFrameData(frameData);
    ScopeExit cleanupOnFailure([&frame]()noexcept{ CleanupX11Frame(frame); });

    ::Display* const display = XOpenDisplay(nullptr);
    SetX11Display(frameData, display);
    if(!display){
        CleanupX11Frame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame X11 display connection failed"));
        return false;
    }

    const i32 screen = DefaultScreen(display);
    const i32 displayWidth = DisplayWidth(display, screen);
    const i32 displayHeight = DisplayHeight(display, screen);
    const u16 requestedWidth = frameData.width();
    const u16 requestedHeight = frameData.height();
    const u16 windowWidth = ClampInitialWindowDimension(requestedWidth, displayWidth);
    const u16 windowHeight = ClampInitialWindowDimension(requestedHeight, displayHeight);
    if(windowWidth == 0u || windowHeight == 0u){
        CleanupX11Frame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame X11 display dimensions are invalid: {}x{}"), displayWidth, displayHeight);
        return false;
    }
    if(windowWidth != requestedWidth || windowHeight != requestedHeight){
        NWB_LOGGER_ESSENTIAL_INFO(FOUNDATION_TEXT("Frame X11 client size clamped from {}x{} to {}x{} for display {}x{}"), requestedWidth, requestedHeight, windowWidth, windowHeight, displayWidth, displayHeight);
    }
    frameData.width() = windowWidth;
    frameData.height() = windowHeight;

    const i32 centeredX = (displayWidth - static_cast<i32>(windowWidth)) >> 1;
    const i32 centeredY = (displayHeight - static_cast<i32>(windowHeight)) >> 1;
    const i32 x = centeredX > 0 ? centeredX : 0;
    const i32 y = centeredY > 0 ? centeredY : 0;

    SetX11Window(frameData, XCreateSimpleWindow(display, RootWindow(display, screen), x, y, windowWidth, windowHeight, 0, BlackPixel(display, screen), WhitePixel(display, screen)));
    if(GetX11Window(frameData) == 0){
        CleanupX11Frame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame X11 window creation failed"));
        return false;
    }

    s_Keyboard.configure(*display);

    XSelectInput(display, GetX11Window(frameData), s_WindowEventMask);
    XStoreName(display, GetX11Window(frameData), windowTitle.data());

    // Publish _NET_WM_PID so process managers can find the window and close it, even headless on XWayland where PID lookup is key.
    {
        const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", False);
        if(pidAtom != None){
            const long pidValue = static_cast<long>(::getpid());
            XChangeProperty(display, GetX11Window(frameData), pidAtom, XA_CARDINAL, s_X11CardinalPropertyFormatBits, PropModeReplace, reinterpret_cast<const unsigned char*>(&pidValue), 1);
        }
    }

    SetDeleteWindowMessage(frameData, XInternAtom(display, "WM_DELETE_WINDOW", False));
    if(GetDeleteWindowMessage(frameData) == None){
        CleanupX11Frame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame X11 window close protocol registration failed"));
        return false;
    }
    Atom deleteWindowMessage = GetDeleteWindowMessage(frameData);
    if(XSetWMProtocols(display, GetX11Window(frameData), &deleteWindowMessage, 1) == 0){
        CleanupX11Frame(frame);
        cleanupOnFailure.release();
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Frame X11 window close protocol installation failed"));
        return false;
    }

    cleanupOnFailure.release();
    return true;
}

Expected<GlobalUniquePtr<IClipboardService>> CreateX11FrameClipboard(Frame& frame){
    Display* const display = static_cast<::Display*>(frame.data<Common::LinuxFrame>().nativeDisplay());
    FOUNDATION_FATAL_ASSERT(display);
    return CreateX11ClipboardService(frame.projectObjectArena(), *display);
}

Expected<GlobalUniquePtr<ITextInputService>> CreateX11FrameTextInput(Frame& frame){
    const auto& frameData = frame.data<Common::LinuxFrame>();
    Display* const display = static_cast<::Display*>(frameData.nativeDisplay());
    FOUNDATION_FATAL_ASSERT(display && GetX11Window(frameData));
    auto service = CreateX11TextInputService(frame.projectObjectArena(), *display, frameData.nativeWindowHandle());
    if(service && !(*service)->setFocused(frameData.isActive()))
        FOUNDATION_FATAL_ASSERT(false);
    return service;
}

void ShowX11Frame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();
    ::Display* const display = static_cast<::Display*>(frameData.nativeDisplay());

    XMapRaised(display, GetX11Window(frameData));
    XFlush(display);
}

b8 RunX11Frame(Frame& frame){
    auto& frameData = frame.data<Common::LinuxFrame>();

    Timer lateTime(TimerNow());

    for(;;){
        if(frame.quitRequested())
            return true;

        while(XPending(static_cast<::Display*>(frameData.nativeDisplay())) > 0){
            XEvent event = {};
            XNextEvent(static_cast<::Display*>(frameData.nativeDisplay()), &event);
            if(IClipboardService* const clipboard = frame.tryClipboard()){
                if(DispatchX11ClipboardEvent(*clipboard, event))
                    continue;
            }
            if(ITextInputService* const textInput = frame.tryTextInput()){
                s_Keyboard.synchronizeSession(textInput->activeSession());
                if(
                    event.xany.window == GetX11Window(frameData) && (event.type == KeyPress || event.type == KeyRelease)
                    && s_Keyboard.isRetiredDuplicate(event.xkey)
                ){
                    // XIM must not re-forward an old context's echo into its replacement context.
                    continue;
                }
                const XEvent physical = event;
                if(FilterX11TextInputEvent(*textInput, event)){
                    // XIM filtering owns text. Normalized physical presses/releases must remain balanced.
                    if(physical.xany.window == GetX11Window(frameData) && (physical.type == KeyPress || physical.type == KeyRelease) && physical.xkey.keycode != 0u)
                        s_Keyboard.process(frame, physical.xkey, false);
                    else if(physical.type == FocusIn || physical.type == FocusOut || physical.type == UnmapNotify || physical.type == DestroyNotify){
                        if(!ProcessEvent(frame, physical))
                            return true;
                    }
                    continue;
                }
            }
            if(!ProcessEvent(frame, event))
                return true;
        }

        const auto windowState = QueryWindowState(frame);
        if(!windowState)
            return false;

        if(const Optional<TStringView> title = frame.syncGraphicsWindowState(windowState->width, windowState->height, windowState->windowVisible, windowState->windowIsInFocus)){
            ::Display* const display = static_cast<::Display*>(frameData.nativeDisplay());
            XStoreName(display, GetX11Window(frameData), title->data());
            XFlush(display);
        }

        Timer currentTime(TimerNow());
        const f32 delta = DurationInSeconds<f32>(currentTime, lateTime);
        lateTime = currentTime;

        if(!frame.update(delta))
            return false;
        if(frame.quitRequested())
            return true;

        if(!windowState->windowVisible || !windowState->windowIsInFocus)
            SleepMS(FrameDetail::s_InactivePollSleepMs);
    }

    return false;
}

void CleanupX11Frame(Frame& frame)noexcept{
    auto& frameData = frame.data<Common::LinuxFrame>();

    if(::Display* const display = static_cast<::Display*>(frameData.nativeDisplay())){
        if(GetX11Window(frameData)){
            XDestroyWindow(display, GetX11Window(frameData));
            SetX11Window(frameData, 0);
        }

        XCloseDisplay(display);
        SetX11Display(frameData, nullptr);
    }

    SetDeleteWindowMessage(frameData, None);
    s_Keyboard.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

