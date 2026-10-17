// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "frame_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>

#include "input_helpers.h"

#include <core/common/log.h>
#include <core/os/win32/text_input.h>
#include <core/os/win32/input_message.h>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_message_loop{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Win32MessagePumpResult{
    enum Enum : u8{
        Continue,
        SkipUpdate,
        Quit,
        Failed
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename OnDestroyFunc, typename SetActiveFunc>
[[nodiscard]] static bool HandleWin32FrameLifecycleMessage(HWND hwnd, UINT message, WPARAM wParam, OnDestroyFunc&& onDestroy, SetActiveFunc&& setActive, LRESULT& outResult){
    switch(message){
    case WM_DESTROY:
        onDestroy();
        PostQuitMessage(0);
        outResult = 0;
        return true;
    case WM_CLOSE:
        outResult = 0;
        if(!DestroyWindow(hwnd)){
            const DWORD nativeError = GetLastError();
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 close failed; native_domain=Win32 native_code={}"), nativeError);
        }
        return true;
    case WM_ACTIVATE:
        setActive(LOWORD(wParam) != WA_INACTIVE);
        outResult = 0;
        return true;
    default:
        return false;
    }
}


[[nodiscard]] static Win32MessagePumpResult::Enum HandleWin32QuitMessage(const WPARAM exitCode){
    if(exitCode != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 quit signaled failure; exit_domain=application exit_code={}"), static_cast<i32>(exitCode));
        return Win32MessagePumpResult::Failed;
    }
    return Win32MessagePumpResult::Quit;
}

template<typename IsActiveFunc>
[[nodiscard]] static Win32MessagePumpResult::Enum PumpWin32FrameMessages(IsActiveFunc&& isActive){
    MSG message = {};

    if(isActive()){
        while(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)){
            if(message.message == WM_QUIT)
                return HandleWin32QuitMessage(message.wParam);

            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        return Win32MessagePumpResult::Continue;
    }

    const int received = GetMessage(&message, nullptr, 0, 0);
    if(received == -1){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 message retrieval failed; native_domain=Win32 native_code={}"), nativeError);
        return Win32MessagePumpResult::Failed;
    }
    if(received == 0)
        return HandleWin32QuitMessage(message.wParam);

    TranslateMessage(&message);
    DispatchMessage(&message);
    return Win32MessagePumpResult::SkipUpdate;
}

template<typename IsActiveFunc, typename BeforeUpdateFunc, typename UpdateFunc>
[[nodiscard]] static bool RunWin32TimedFrameLoop(IsActiveFunc&& isActive, BeforeUpdateFunc&& beforeUpdate, UpdateFunc&& update){
    Timer lateTime(TimerNow());

    for(;;){
        switch(PumpWin32FrameMessages(isActive)){
        case Win32MessagePumpResult::Quit:
            return true;
        case Win32MessagePumpResult::Failed:
            return false;
        case Win32MessagePumpResult::SkipUpdate:
            continue;
        case Win32MessagePumpResult::Continue:
            break;
        }

        if(!beforeUpdate())
            return false;

        const f32 timeDifference = ConsumeTimerDeltaSeconds<f32>(lateTime);
        if(!update(timeDifference))
            break;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Frame* s_Frame = nullptr;

static constexpr TStringView s_ClassName = NWB_TEXT("NWB_FRAME");
static constexpr i32 s_PaintFailureExitCode = 1;

static constexpr usize s_Win32ExtendedKeyBit = 24u;
static constexpr usize s_Win32ScancodeShift = 16u;
static constexpr usize s_Win32PreviousKeyStateBit = 30u;
static constexpr usize s_Win32ScancodeMask = 0x1ffu;
static constexpr usize s_Win32VirtualScancodeMask = 0xffu;
static constexpr usize s_Win32WordMask = 0xffffu;
static constexpr int s_Win32KeyDownMask = 0x8000;
static constexpr int s_Win32KeyToggleMask = 0x0001;
static constexpr u32 s_AsciiDigitFirst = '0';
static constexpr u32 s_AsciiDigitLast = '9';
static constexpr u32 s_AsciiUpperFirst = 'A';
static constexpr u32 s_AsciiUpperLast = 'Z';


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool EnableProcessDpiAwareness(){
    if(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return true;

    if(!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame Win32 per-monitor DPI awareness setup failed: {}"), GetLastError());
        return false;
    }
    return true;
}

static bool IsExtendedKey(LPARAM lParam)noexcept{
    return (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32ExtendedKeyBit)) != 0;
}

static i32 TranslateScancode(LPARAM lParam)noexcept{
    return static_cast<i32>((static_cast<usize>(lParam) >> s_Win32ScancodeShift) & s_Win32ScancodeMask);
}

static i32 TranslateModifiers()noexcept{
    i32 mods = 0;

    if(GetKeyState(VK_SHIFT) & s_Win32KeyDownMask)
        mods |= InputModifier::Shift;
    if(GetKeyState(VK_CONTROL) & s_Win32KeyDownMask)
        mods |= InputModifier::Control;
    if(GetKeyState(VK_MENU) & s_Win32KeyDownMask)
        mods |= InputModifier::Alt;
    if((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & s_Win32KeyDownMask)
        mods |= InputModifier::Super;
    if(GetKeyState(VK_CAPITAL) & s_Win32KeyToggleMask)
        mods |= InputModifier::CapsLock;
    if(GetKeyState(VK_NUMLOCK) & s_Win32KeyToggleMask)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslateShiftKey(LPARAM lParam)noexcept{
    const UINT scancode = static_cast<UINT>((static_cast<usize>(lParam) >> s_Win32ScancodeShift) & s_Win32VirtualScancodeMask);
    switch(MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX)){
    case VK_RSHIFT:
        return Key::RightShift;
    case VK_LSHIFT:
    default:
        return Key::LeftShift;
    }
}

static i32 TranslateNavigationKey(i32 extendedKey, i32 keypadKey, LPARAM lParam)noexcept{
    return IsExtendedKey(lParam) ? extendedKey : keypadKey;
}

static i32 TranslateKey(WPARAM wParam, LPARAM lParam)noexcept{
    const u32 vk = static_cast<u32>(wParam);

    if(vk >= s_AsciiDigitFirst && vk <= s_AsciiDigitLast)
        return static_cast<i32>(Key::Number0 + (vk - s_AsciiDigitFirst));

    if(vk >= s_AsciiUpperFirst && vk <= s_AsciiUpperLast)
        return static_cast<i32>(Key::A + (vk - s_AsciiUpperFirst));

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

static i32 SignedLowWord(LPARAM value)noexcept{
    return static_cast<i32>(static_cast<i16>(static_cast<u16>(static_cast<usize>(value) & s_Win32WordMask)));
}

static i32 SignedHighWord(LPARAM value)noexcept{
    return static_cast<i32>(static_cast<i16>(static_cast<u16>((static_cast<usize>(value) >> s_Win32ScancodeShift) & s_Win32WordMask)));
}

static i32 TranslateMouseButton(UINT message, WPARAM wParam)noexcept{
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

    if((GetKeyState(VK_LBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_RBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_MBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_XBUTTON1) & s_Win32KeyDownMask) || (GetKeyState(VK_XBUTTON2) & s_Win32KeyDownMask))
        return;

    if(!ReleaseCapture()){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 pointer capture release failed; native_domain=Win32 native_code={}"), nativeError);
    }
}

static void DispatchMousePosition(Frame& frame, LPARAM lParam){
    frame.input().mousePosUpdate(static_cast<f64>(SignedLowWord(lParam)), static_cast<f64>(SignedHighWord(lParam)));
}

static void DispatchScreenMousePosition(HWND hwnd, Frame& frame, LPARAM lParam){
    POINT point = { SignedLowWord(lParam), SignedHighWord(lParam) };
    if(ScreenToClient(hwnd, &point))
        frame.input().mousePosUpdate(static_cast<f64>(point.x), static_cast<f64>(point.y));
    else{
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_WARNING(NWB_TEXT("Frame Win32 mouse position conversion failed; native_domain=Win32 native_code={}"), nativeError);
    }
}

static void DispatchKeyEvent(Frame& frame, WPARAM wParam, LPARAM lParam, i32 action){
    const i32 key = TranslateKey(wParam, lParam);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers());
    const u32 count = action == InputAction::Release ? 1u : Win32MessageRepeatCount(lParam);
    for(u32 index = 0u; index < count; ++index)
        frame.input().keyboardUpdate(key, TranslateScancode(lParam), index == 0u ? action : InputAction::Repeat, mods);
}

static void DispatchCharInput(Frame& frame, WPARAM wParam, LPARAM lParam){
#if defined(NWB_UNICODE)
    if(auto* textInput = frame.tryTextInput()){
        const auto unicode = DecodeWin32FallbackCharInput(*textInput, static_cast<u32>(wParam));
        if(unicode){
            for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
                DispatchUnicodeInput(frame, *unicode);
        }
    }
#else
    for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
        DispatchUnicodeInput(frame, static_cast<u32>(wParam & s_AnsiCharMask));
#endif
}

static void DispatchTextInputFocus(Frame& frame, const bool focused){
    if(auto* textInput = frame.tryTextInput()){
        if(!ResetWin32FallbackCharInput(*textInput))
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input character reset failed"));
        if(!textInput->setFocused(focused))
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input focus update failed"));
    }
}

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = s_Frame){
        if(uMsg == WM_NCDESTROY && frame->data<Common::WinFrame>().hwnd() == hwnd)
            frame->data<Common::WinFrame>().setHwnd(nullptr);
        if(uMsg == WM_SETFOCUS || uMsg == WM_KILLFOCUS){
            const bool focused = uMsg == WM_SETFOCUS;
            DispatchTextInputFocus(*frame, focused);
            frame->input().windowFocusUpdate(focused);
            return 0;
        }
        LRESULT lifecycleResult;
        if(
            __hidden_win32_message_loop::HandleWin32FrameLifecycleMessage(
                hwnd, uMsg, wParam, []()noexcept{},
                [&](const bool isActive){
                    frame->data<Common::WinFrame>().setActive(isActive);
                    DispatchTextInputFocus(*frame, isActive && GetFocus() == hwnd);
                    frame->input().windowFocusUpdate(isActive);
                },
                lifecycleResult
            )
        )
            return lifecycleResult;

        const bool ordinaryText = uMsg == WM_CHAR || uMsg == WM_SYSCHAR || (uMsg == WM_UNICHAR && wParam != UNICODE_NOCHAR);
        const i32 textScancode = ordinaryText ? TranslateScancode(lParam) : 0;
        // Text without a producing scan code is explicit native text, independent of a physical shortcut.
        if(ordinaryText && textScancode != 0 && frame->input().keyboardTextBlocked(textScancode)){
            if(auto* textInput = frame->tryTextInput()){
                if(!ResetWin32FallbackCharInput(*textInput))
                    NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 suppressed text-input character reset failed"));
            }
            return 0;
        }

        if(auto* textInput = frame->tryTextInput()){
            const auto forwardedLParam = ResolveWin32TextInputContextMessage(*textInput, uMsg, wParam, lParam);
            if(forwardedLParam)
                return DefWindowProc(hwnd, uMsg, wParam, static_cast<LPARAM>(*forwardedLParam));
            if(DispatchWin32TextInputMessage(*textInput, uMsg, wParam, lParam))
                return 0;
        }

        PAINTSTRUCT ps;

        switch(uMsg){
        case WM_PAINT: {
            if(BeginPaint(hwnd, &ps)){
                // EndPaint is a command whose SDK return is always nonzero.
                EndPaint(hwnd, &ps);
            }
            else{
                PostQuitMessage(s_PaintFailureExitCode);
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 paint preparation failed; exit_domain=application exit_code={}"), s_PaintFailureExitCode);
            }
        }
        return 0;

        case WM_KEYDOWN: {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32PreviousKeyStateBit)) ? InputAction::Repeat : InputAction::Press;
            DispatchKeyEvent(*frame, wParam, lParam, action);
        }
        return 0;

        case WM_SYSKEYDOWN: {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32PreviousKeyStateBit)) ? InputAction::Repeat : InputAction::Press;
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
            DispatchCharInput(*frame, wParam, lParam);
        }
        return 0;

        case WM_SYSCHAR: {
            DispatchCharInput(*frame, wParam, lParam);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_UNICHAR: {
            if(wParam == UNICODE_NOCHAR)
                return TRUE;
            if(GetFocus() != hwnd || !frame->data<Common::WinFrame>().isActive())
                return 0;
            if(auto* textInput = frame->tryTextInput()){
                if(!ResetWin32FallbackCharInput(*textInput))
                    NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input character reset failed"));
            }
            for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
                DispatchUnicodeInput(*frame, static_cast<u32>(wParam));
        }
        return 0;

        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0u };
            if(!TrackMouseEvent(&tracking)){
                const DWORD nativeError = GetLastError();
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 mouse-leave tracking failed; native_domain=Win32 native_code={}"), nativeError);
            }
            DispatchMousePosition(*frame, lParam);
        }
        return 0;

        case WM_MOUSELEAVE:
            frame->input().pointerLeave();
            return 0;

        case WM_CAPTURECHANGED:
            if(reinterpret_cast<HWND>(lParam) != hwnd)
                frame->input().pointerCaptureLost();
            return 0;

        case WM_CANCELMODE:
            frame->input().pointerCaptureLost();
            if(GetCapture() == hwnd && !ReleaseCapture()){
                const DWORD nativeError = GetLastError();
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 pointer capture release failed; native_domain=Win32 native_code={}"), nativeError);
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
    if(!FrameDetail::EnableProcessDpiAwareness())
        return false;

    const UINT initialDpi = GetDpiForSystem();
    if(initialDpi == 0u){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame Win32 system DPI query failed"));
        return false;
    }

    const TStringView windowTitle = windowTitleOrDefault();
    constexpr DWORD s_WindowExtendedStyle = 0;
    constexpr DWORD s_WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
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
        wc.lpszClassName = FrameDetail::s_ClassName.data();
    }
    const ATOM windowClass = RegisterClassEx(&wc);
    if(!windowClass){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window registration failed; native_domain=Win32 native_code={}"), nativeError);
        return false;
    }
    frameData.setWindowClass(windowClass);

    RECT workArea = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    {
        const POINT origin = { 0, 0 };
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        if(monitor && GetMonitorInfo(monitor, &monitorInfo))
            workArea = monitorInfo.rcWork;
    }

    RECT decorationRect = { 0, 0, 0, 0 };
    if(!AdjustWindowRectExForDpi(&decorationRect, s_WindowStyle, FALSE, s_WindowExtendedStyle, initialDpi)){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed; native_domain=Win32 native_code={}"), nativeError);
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
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Frame Win32 client size clamped from {}x{} to {}x{} for work area {}x{}"), requestedWidth, requestedHeight, windowWidth, windowHeight, workAreaWidth, workAreaHeight);
    }
    frameData.width() = windowWidth;
    frameData.height() = windowHeight;

    RECT rc = { 0, 0, static_cast<i32>(windowWidth), static_cast<i32>(windowHeight) };
    if(!AdjustWindowRectExForDpi(&rc, s_WindowStyle, FALSE, s_WindowExtendedStyle, initialDpi)){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed; native_domain=Win32 native_code={}"), nativeError);
        return false;
    }

    const auto actualWidth = rc.right - rc.left;
    const auto actualHeight = rc.bottom - rc.top;
    const auto x = workArea.left + (workAreaWidth > actualWidth ? ((workAreaWidth - actualWidth) >> 1) : 0);
    const auto y = workArea.top + (workAreaHeight > actualHeight ? ((workAreaHeight - actualHeight) >> 1) : 0);

    HWND hwnd = CreateWindowEx(s_WindowExtendedStyle, wc.lpszClassName, windowTitle.data(), s_WindowStyle, x, y, actualWidth, actualHeight, nullptr, nullptr, wc.hInstance, nullptr);
    frameData.setHwnd(hwnd);
    if(!frameData.hwnd()){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window creation failed; native_domain=Win32 native_code={}"), nativeError);
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
    const bool loopResult = __hidden_win32_message_loop::RunWin32TimedFrameLoop([&]()noexcept{ return data<Common::WinFrame>().isActive(); },
        [&](){
            RECT rect = {};
            if(!GetClientRect(data<Common::WinFrame>().hwnd(), &rect)){
                const DWORD nativeError = GetLastError();
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 client rectangle query failed; native_domain=Win32 native_code={}"), nativeError);
                return false;
            }
            const bool windowVisible = (rect.right > rect.left) && (rect.bottom > rect.top);
            const u32 width = windowVisible ? static_cast<u32>(rect.right - rect.left) : 0;
            const u32 height = windowVisible ? static_cast<u32>(rect.bottom - rect.top) : 0;
            const bool windowIsInFocus = GetForegroundWindow() == data<Common::WinFrame>().hwnd();
            if(const Optional<TStringView> title = syncGraphicsWindowState(width, height, windowVisible, windowIsInFocus)){
#ifdef NWB_UNICODE
                const bool titleChanged = SetWindowTextW(data<Common::WinFrame>().hwnd(), title->data()) != FALSE;
#else
                const bool titleChanged = SetWindowTextA(data<Common::WinFrame>().hwnd(), title->data()) != FALSE;
#endif
                if(!titleChanged){
                    const DWORD nativeError = GetLastError();
                    NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 window title update failed; native_domain=Win32 native_code={}"), nativeError);
                    return false;
                }
            }
            return true;
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

Expected<GlobalUniquePtr<IClipboardService>> Frame::createPlatformClipboardService(){
    return CreateClipboardService(m_projectObjectArena, data<Common::WinFrame>().hwnd());
}

Expected<GlobalUniquePtr<ITextInputService>> Frame::createPlatformTextInputService(){
    const auto hwnd = data<Common::WinFrame>().hwnd();
    auto service = CreateTextInputService(m_projectObjectArena, hwnd);
    if(!service)
        return MakeUnexpected(service.error());
    if(!(*service)->setFocused(GetFocus() == hwnd))
        return MakeUnexpected(Failure{});
    return service;
}

void Frame::setupPlatform(void* inst)noexcept{
    FrameDetail::s_Frame = this;
    auto& frameData = data<Common::WinFrame>();
    frameData.setActive(false);
    frameData.setInstance(reinterpret_cast<HINSTANCE>(inst));
    frameData.setHwnd(nullptr);
    frameData.setWindowClass(0u);
}
void Frame::cleanupPlatform()noexcept{
    if(FrameDetail::s_Frame == this)
        FrameDetail::s_Frame = nullptr;
    auto& frameData = data<Common::WinFrame>();
    const HWND window = frameData.hwnd();
    const ATOM windowClass = frameData.windowClass();
    frameData.setHwnd(nullptr);
    frameData.setWindowClass(0u);
    frameData.setActive(false);

    const bool windowFailed = window && !DestroyWindow(window);
    const DWORD windowError = windowFailed ? GetLastError() : ERROR_SUCCESS;
    const bool classFailed = windowClass && !UnregisterClass(MAKEINTATOM(windowClass), frameData.instance());
    const DWORD classError = classFailed ? GetLastError() : ERROR_SUCCESS;

    if(windowFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame window destruction failed; native_domain=Win32 native_code={}", windowError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "frame_window_lifecycle", .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
    if(classFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame window class release failed; native_domain=Win32 native_code={}", classError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "frame_window_lifecycle", .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

