// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>
#include <core/common/win32_message_loop.h>

#include "input_helpers.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// in windows, the frame is a singleton
static Frame* s_Frame = nullptr;


static HMODULE GetUser32Module(){
    HMODULE module = GetModuleHandleW(L"user32.dll");
    if(!module)
        module = LoadLibraryW(L"user32.dll");
    return module;
}

static void EnableProcessDpiAwareness(){
    HMODULE user32 = GetUser32Module();
    if(!user32)
        return;

    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    const auto setProcessDpiAwarenessContext =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
    ;
    if(setProcessDpiAwarenessContext){
        constexpr isize PerMonitorAwareV2 = -4;
        if(setProcessDpiAwarenessContext(reinterpret_cast<HANDLE>(PerMonitorAwareV2)))
            return;
    }

    using SetProcessDpiAwareFn = BOOL(WINAPI*)();
    const auto setProcessDpiAware = reinterpret_cast<SetProcessDpiAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if(setProcessDpiAware)
        setProcessDpiAware();
}

static UINT QueryInitialWindowDpi(){
    constexpr UINT DefaultDpi = 96;

    HMODULE user32 = GetUser32Module();
    if(user32){
        using GetDpiForSystemFn = UINT(WINAPI*)();
        const auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if(getDpiForSystem){
            const UINT dpi = getDpiForSystem();
            if(dpi != 0)
                return dpi;
        }
    }

    HDC screenDc = GetDC(nullptr);
    if(!screenDc)
        return DefaultDpi;

    const i32 dpi = GetDeviceCaps(screenDc, LOGPIXELSX);
    ReleaseDC(nullptr, screenDc);

    return dpi > 0 ? static_cast<UINT>(dpi) : DefaultDpi;
}

static bool AdjustWindowRectForDpi(RECT& rect, DWORD style, BOOL hasMenu, DWORD styleEx, UINT dpi){
    HMODULE user32 = GetUser32Module();
    if(user32){
        using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        const auto adjustWindowRectExForDpi =
            reinterpret_cast<AdjustWindowRectExForDpiFn>(GetProcAddress(user32, "AdjustWindowRectExForDpi"))
        ;
        if(adjustWindowRectExForDpi)
            return adjustWindowRectExForDpi(&rect, style, hasMenu, styleEx, dpi) != FALSE;
    }

    return AdjustWindowRectEx(&rect, style, hasMenu, styleEx) != FALSE;
}

static u16 ClampInitialWindowDimension(const u16 requestedDimension, const i32 maxClientDimension){
    if(maxClientDimension <= 0)
        return requestedDimension;

    const u32 clampedDimension = Min<u32>(static_cast<u32>(requestedDimension), static_cast<u32>(maxClientDimension));
    return static_cast<u16>(Min<u32>(clampedDimension, s_MaxU16));
}

static bool IsExtendedKey(LPARAM lParam){
    return (static_cast<usize>(lParam) & (static_cast<usize>(1) << 24)) != 0;
}

static i32 TranslateScancode(LPARAM lParam){
    return static_cast<i32>((static_cast<usize>(lParam) >> 16) & 0x1ffu);
}

static i32 TranslateModifiers(){
    i32 mods = 0;

    if(GetKeyState(VK_SHIFT) & 0x8000)
        mods |= InputModifier::Shift;
    if(GetKeyState(VK_CONTROL) & 0x8000)
        mods |= InputModifier::Control;
    if(GetKeyState(VK_MENU) & 0x8000)
        mods |= InputModifier::Alt;
    if((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= InputModifier::Super;
    if(GetKeyState(VK_CAPITAL) & 0x0001)
        mods |= InputModifier::CapsLock;
    if(GetKeyState(VK_NUMLOCK) & 0x0001)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslateShiftKey(LPARAM lParam){
    const UINT scancode = static_cast<UINT>((static_cast<usize>(lParam) >> 16) & 0xffu);
    switch(MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX)){
    case VK_RSHIFT:
        return Key::RightShift;
    case VK_LSHIFT:
    default:
        return Key::LeftShift;
    }
}

static i32 TranslateNavigationKey(i32 extendedKey, i32 keypadKey, LPARAM lParam){
    return IsExtendedKey(lParam) ? extendedKey : keypadKey;
}

static i32 TranslateKey(WPARAM wParam, LPARAM lParam){
    const u32 vk = static_cast<u32>(wParam);

    if(vk >= '0' && vk <= '9')
        return static_cast<i32>(Key::Number0 + (vk - '0'));

    if(vk >= 'A' && vk <= 'Z')
        return static_cast<i32>(Key::A + (vk - 'A'));

    if(vk >= VK_F1 && vk <= VK_F24)
        return static_cast<i32>(Key::F1 + (vk - VK_F1));

    if(vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return static_cast<i32>(Key::Keypad0 + (vk - VK_NUMPAD0));

    switch(vk){
    case VK_SPACE: return Key::Space;
    case VK_OEM_7: return Key::Apostrophe;
    case VK_OEM_COMMA: return Key::Comma;
    case VK_OEM_MINUS: return Key::Minus;
    case VK_OEM_PERIOD: return Key::Period;
    case VK_OEM_2: return Key::Slash;
    case VK_OEM_1: return Key::Semicolon;
    case VK_OEM_PLUS: return Key::Equal;
    case VK_OEM_4: return Key::LeftBracket;
    case VK_OEM_5: return Key::Backslash;
    case VK_OEM_6: return Key::RightBracket;
    case VK_OEM_3: return Key::GraveAccent;
    case VK_OEM_102: return Key::World2;

    case VK_ESCAPE: return Key::Escape;
    case VK_RETURN: return IsExtendedKey(lParam) ? Key::KeypadEnter : Key::Enter;
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_INSERT: return TranslateNavigationKey(Key::Insert, Key::Keypad0, lParam);
    case VK_DELETE: return TranslateNavigationKey(Key::Delete, Key::KeypadDecimal, lParam);
    case VK_RIGHT: return TranslateNavigationKey(Key::Right, Key::Keypad6, lParam);
    case VK_LEFT: return TranslateNavigationKey(Key::Left, Key::Keypad4, lParam);
    case VK_DOWN: return TranslateNavigationKey(Key::Down, Key::Keypad2, lParam);
    case VK_UP: return TranslateNavigationKey(Key::Up, Key::Keypad8, lParam);
    case VK_PRIOR: return TranslateNavigationKey(Key::PageUp, Key::Keypad9, lParam);
    case VK_NEXT: return TranslateNavigationKey(Key::PageDown, Key::Keypad3, lParam);
    case VK_HOME: return TranslateNavigationKey(Key::Home, Key::Keypad7, lParam);
    case VK_END: return TranslateNavigationKey(Key::End, Key::Keypad1, lParam);
    case VK_CLEAR: return Key::Keypad5;
    case VK_CAPITAL: return Key::CapsLock;
    case VK_SCROLL: return Key::ScrollLock;
    case VK_NUMLOCK: return Key::NumLock;
    case VK_SNAPSHOT: return Key::PrintScreen;
    case VK_PAUSE: return Key::Pause;

    case VK_DECIMAL: return Key::KeypadDecimal;
    case VK_DIVIDE: return Key::KeypadDivide;
    case VK_MULTIPLY: return Key::KeypadMultiply;
    case VK_SUBTRACT: return Key::KeypadSubtract;
    case VK_ADD: return Key::KeypadAdd;
#ifdef VK_OEM_NEC_EQUAL
    case VK_OEM_NEC_EQUAL: return Key::KeypadEqual;
#endif

    case VK_SHIFT: return TranslateShiftKey(lParam);
    case VK_CONTROL: return IsExtendedKey(lParam) ? Key::RightControl : Key::LeftControl;
    case VK_MENU: return IsExtendedKey(lParam) ? Key::RightAlt : Key::LeftAlt;
    case VK_LSHIFT: return Key::LeftShift;
    case VK_RSHIFT: return Key::RightShift;
    case VK_LCONTROL: return Key::LeftControl;
    case VK_RCONTROL: return Key::RightControl;
    case VK_LMENU: return Key::LeftAlt;
    case VK_RMENU: return Key::RightAlt;
    case VK_LWIN: return Key::LeftSuper;
    case VK_RWIN: return Key::RightSuper;
    case VK_APPS: return Key::Menu;
    default:
        return Key::Unknown;
    }
}

static i32 SignedLowWord(LPARAM value){
    return static_cast<i32>(static_cast<i16>(static_cast<u16>(static_cast<usize>(value) & 0xffffu)));
}

static i32 SignedHighWord(LPARAM value){
    return static_cast<i32>(static_cast<i16>(static_cast<u16>((static_cast<usize>(value) >> 16) & 0xffffu)));
}

static i32 TranslateMouseButton(UINT message, WPARAM wParam){
    switch(message){
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return MouseButton::Left;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return MouseButton::Right;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return MouseButton::Middle;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        switch(HIWORD(wParam)){
        case XBUTTON1:
            return MouseButton::Button4;
        case XBUTTON2:
            return MouseButton::Button5;
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static bool IsTextInputCodePoint(u32 unicode){
    if(unicode < 32u || unicode == 127u)
        return false;
    if(unicode > 0x10ffffu)
        return false;
    if(unicode >= 0xd800u && unicode <= 0xdfffu)
        return false;
    return true;
}

static void DispatchUnicodeInput(Frame& frame, u32 unicode){
    if(IsTextInputCodePoint(unicode))
        frame.input().keyboardCharInput(unicode, TranslateModifiers());
}

static void CaptureMouse(HWND hwnd){
    if(GetCapture() != hwnd)
        SetCapture(hwnd);
}

static void ReleaseMouseIfNoButtonIsDown(HWND hwnd){
    if(GetCapture() != hwnd)
        return;

    if(
        (GetKeyState(VK_LBUTTON) & 0x8000)
        || (GetKeyState(VK_RBUTTON) & 0x8000)
        || (GetKeyState(VK_MBUTTON) & 0x8000)
        || (GetKeyState(VK_XBUTTON1) & 0x8000)
        || (GetKeyState(VK_XBUTTON2) & 0x8000)
    )
        return;

    ReleaseCapture();
}

static void DispatchMousePosition(Frame& frame, LPARAM lParam){
    frame.input().mousePosUpdate(static_cast<f64>(SignedLowWord(lParam)), static_cast<f64>(SignedHighWord(lParam)));
}

static void DispatchScreenMousePosition(HWND hwnd, Frame& frame, LPARAM lParam){
    POINT point = { SignedLowWord(lParam), SignedHighWord(lParam) };
    if(ScreenToClient(hwnd, &point))
        frame.input().mousePosUpdate(static_cast<f64>(point.x), static_cast<f64>(point.y));
}

static void DispatchKeyEvent(Frame& frame, WPARAM wParam, LPARAM lParam, i32 action){
    const i32 key = TranslateKey(wParam, lParam);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers());
    frame.input().keyboardUpdate(key, TranslateScancode(lParam), action, mods);
}

static void DispatchCharInput(Frame& frame, WPARAM wParam){
#if defined(NWB_UNICODE)
    static u16 highSurrogate = 0;
    const u32 codeUnit = static_cast<u32>(wParam);

    if(codeUnit >= 0xd800u && codeUnit <= 0xdbffu){
        highSurrogate = static_cast<u16>(codeUnit);
        return;
    }

    if(codeUnit >= 0xdc00u && codeUnit <= 0xdfffu){
        if(highSurrogate != 0){
            const u32 unicode = 0x10000u
                + ((static_cast<u32>(highSurrogate) - 0xd800u) << 10)
                + (codeUnit - 0xdc00u)
            ;
            highSurrogate = 0;
            DispatchUnicodeInput(frame, unicode);
        }
        return;
    }

    highSurrogate = 0;
    DispatchUnicodeInput(frame, codeUnit);
#else
    DispatchUnicodeInput(frame, static_cast<u32>(wParam & 0xffu));
#endif
}

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = s_Frame){
        LRESULT lifecycleResult = 0;
        if(
            Common::HandleWin32FrameLifecycleMessage(
                hwnd,
                uMsg,
                wParam,
                [](){},
                [&](const bool isActive){ frame->data<Common::WinFrame>().setActive(isActive); },
                lifecycleResult
            )
        )
            return lifecycleResult;

        PAINTSTRUCT ps;

        switch(uMsg){
        case WM_PAINT: {
            bool ret = false;
            if(auto hdc = BeginPaint(hwnd, &ps)){
                static_cast<void>(hdc);
                ret = frame->render();
                EndPaint(hwnd, &ps);
            }
            if(!ret)
                PostQuitMessage(0);
        }
        return 0;

        case WM_KEYDOWN: {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << 30))
                ? InputAction::Repeat
                : InputAction::Press
            ;
            DispatchKeyEvent(*frame, wParam, lParam, action);
        }
        return 0;

        case WM_SYSKEYDOWN: {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << 30))
                ? InputAction::Repeat
                : InputAction::Press
            ;
            DispatchKeyEvent(*frame, wParam, lParam, action);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_KEYUP: {
            DispatchKeyEvent(*frame, wParam, lParam, InputAction::Release);
        }
        return 0;

        case WM_SYSKEYUP: {
            DispatchKeyEvent(*frame, wParam, lParam, InputAction::Release);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_CHAR: {
            DispatchCharInput(*frame, wParam);
        }
        return 0;

        case WM_SYSCHAR: {
            DispatchCharInput(*frame, wParam);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_UNICHAR: {
            if(wParam == UNICODE_NOCHAR)
                return TRUE;
            DispatchUnicodeInput(*frame, static_cast<u32>(wParam));
        }
        return 0;

        case WM_MOUSEMOVE: {
            DispatchMousePosition(*frame, lParam);
        }
        return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            DispatchMousePosition(*frame, lParam);
            frame->input().mouseButtonUpdate(TranslateMouseButton(uMsg, wParam), InputAction::Press, TranslateModifiers());
            CaptureMouse(hwnd);
        }
        return (uMsg == WM_XBUTTONDOWN) ? TRUE : 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            DispatchMousePosition(*frame, lParam);
            frame->input().mouseButtonUpdate(TranslateMouseButton(uMsg, wParam), InputAction::Release, TranslateModifiers());
            ReleaseMouseIfNoButtonIsDown(hwnd);
        }
        return (uMsg == WM_XBUTTONUP) ? TRUE : 0;

        case WM_MOUSEWHEEL: {
            DispatchScreenMousePosition(hwnd, *frame, lParam);
            frame->input().mouseScrollUpdate(0.0, static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f64>(WHEEL_DELTA));
        }
        return 0;

        case WM_MOUSEHWHEEL: {
            DispatchScreenMousePosition(hwnd, *frame, lParam);
            frame->input().mouseScrollUpdate(static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f64>(WHEEL_DELTA), 0.0);
        }
        return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Frame::init(){
    FrameDetail::EnableProcessDpiAwareness();

    const tchar* ClassName = NWB_TEXT("NWB_FRAME");
    const tchar* AppName = NWB_TEXT("NWBLoader");
    constexpr DWORD StyleEx = 0;
    constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    auto& frameData = data<Common::WinFrame>();

    WNDCLASSEX wc = {};
    {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FrameDetail::WinProc;
        wc.hInstance = frameData.instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        // WNDCLASSEX encodes system color brushes as COLOR_* + 1.
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = ClassName;
    }
    if(!RegisterClassEx(&wc)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window registration failed"));
        return false;
    }

    RECT workArea = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    {
        const POINT origin = { 0, 0 };
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        if(monitor && GetMonitorInfo(monitor, &monitorInfo))
            workArea = monitorInfo.rcWork;
    }

    const UINT initialDpi = FrameDetail::QueryInitialWindowDpi();

    RECT decorationRect = { 0, 0, 0, 0 };
    if(!FrameDetail::AdjustWindowRectForDpi(decorationRect, Style, FALSE, StyleEx, initialDpi)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed"));
        return false;
    }

    const i32 workAreaWidth = workArea.right - workArea.left;
    const i32 workAreaHeight = workArea.bottom - workArea.top;
    const i32 decorationWidth = decorationRect.right - decorationRect.left;
    const i32 decorationHeight = decorationRect.bottom - decorationRect.top;
    const i32 maxClientWidth = workAreaWidth - decorationWidth;
    const i32 maxClientHeight = workAreaHeight - decorationHeight;
    const u16 requestedWidth = frameData.width();
    const u16 requestedHeight = frameData.height();
    const u16 windowWidth = FrameDetail::ClampInitialWindowDimension(requestedWidth, maxClientWidth);
    const u16 windowHeight = FrameDetail::ClampInitialWindowDimension(requestedHeight, maxClientHeight);
    if(windowWidth == 0u || windowHeight == 0u){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame Win32 work area dimensions are invalid: {}x{}"), workAreaWidth, workAreaHeight);
        return false;
    }
    if(windowWidth != requestedWidth || windowHeight != requestedHeight){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("Frame Win32 client size clamped from {}x{} to {}x{} for work area {}x{}"),
            requestedWidth,
            requestedHeight,
            windowWidth,
            windowHeight,
            workAreaWidth,
            workAreaHeight
        );
    }
    frameData.width() = windowWidth;
    frameData.height() = windowHeight;

    RECT rc = { 0, 0, static_cast<i32>(windowWidth), static_cast<i32>(windowHeight) };
    if(!FrameDetail::AdjustWindowRectForDpi(rc, Style, FALSE, StyleEx, initialDpi)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed"));
        return false;
    }

    const auto actualWidth = rc.right - rc.left;
    const auto actualHeight = rc.bottom - rc.top;
    const auto x = workArea.left + (workAreaWidth > actualWidth ? ((workAreaWidth - actualWidth) >> 1) : 0);
    const auto y = workArea.top + (workAreaHeight > actualHeight ? ((workAreaHeight - actualHeight) >> 1) : 0);

    HWND hwnd = CreateWindowEx(
        StyleEx,
        wc.lpszClassName,
        AppName,
        Style,
        x,
        y,
        actualWidth,
        actualHeight,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );
    frameData.setHwnd(hwnd);
    if(!frameData.hwnd()){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window creation failed"));
        return false;
    }

    if(!startup())
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Frame: Using Windows backend."));
    return true;
}
bool Frame::showFrame(){
    ShowWindow(data<Common::WinFrame>().hwnd(), SW_SHOW);
    return true;
}
bool Frame::mainLoop(){
    if(quitRequested())
        return true;

    bool gracefulQuit = false;
    const bool loopResult = Common::RunWin32TimedFrameLoop(
        [&](){ return data<Common::WinFrame>().isActive(); },
        [&](){
            RECT rect = {};
            const bool hasRect = GetClientRect(data<Common::WinFrame>().hwnd(), &rect) != FALSE;
            const bool windowVisible = hasRect && (rect.right > rect.left) && (rect.bottom > rect.top);
            const u32 width = windowVisible ? static_cast<u32>(rect.right - rect.left) : 0;
            const u32 height = windowVisible ? static_cast<u32>(rect.bottom - rect.top) : 0;
            const bool windowIsInFocus = GetForegroundWindow() == data<Common::WinFrame>().hwnd();
            if(const tchar* title = syncGraphicsWindowState(width, height, windowVisible, windowIsInFocus)){
#ifdef NWB_UNICODE
                SetWindowTextW(data<Common::WinFrame>().hwnd(), title);
#else
                SetWindowTextA(data<Common::WinFrame>().hwnd(), title);
#endif
            }
        },
        [&](const f32 timeDifference){
            if(!update(timeDifference))
                return false;
            if(quitRequested()){
                gracefulQuit = true;
                return false;
            }
            return true;
        }
    );
    return loopResult || gracefulQuit;
}

void Frame::setupPlatform(void* inst){
    FrameDetail::s_Frame = this;
    data<Common::WinFrame>().setActive(false);
    data<Common::WinFrame>().setInstance(reinterpret_cast<HINSTANCE>(inst));
}
void Frame::cleanupPlatform(){
    FrameDetail::s_Frame = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

