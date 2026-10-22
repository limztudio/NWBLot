// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"
#include "frame_detail.h"
#include "win32_input.h"


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
#include <global/simd/vector.h>
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

static constexpr StringView s_WarningEventName = "warning";
static constexpr StringView s_WindowLifecycleCategory = "frame_window_lifecycle";


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

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = s_Frame){
        if(uMsg == WM_NCDESTROY && frame->data<Common::WinFrame>().hwnd() == hwnd)
            frame->data<Common::WinFrame>().setHwnd(nullptr);

        Win32FrameInput input(*frame, hwnd);
        if(uMsg == WM_SETFOCUS || uMsg == WM_KILLFOCUS){
            const bool focused = uMsg == WM_SETFOCUS;
            input.dispatchTextInputFocus(focused);
            frame->input().windowFocusUpdate(focused);
            return 0;
        }
        LRESULT lifecycleResult;
        if(
            __hidden_win32_message_loop::HandleWin32FrameLifecycleMessage(
                hwnd, uMsg, wParam, []()noexcept{},
                [&](const bool isActive){
                    frame->data<Common::WinFrame>().setActive(isActive);
                    input.dispatchTextInputFocus(isActive && GetFocus() == hwnd);
                    frame->input().windowFocusUpdate(isActive);
                },
                lifecycleResult
            )
        )
            return lifecycleResult;

        if(const Optional<LRESULT> inputResult = input.handleMessage(uMsg, wParam, lParam))
            return *inputResult;

        if(uMsg == WM_PAINT){
            PAINTSTRUCT ps;
            if(BeginPaint(hwnd, &ps)){
                // EndPaint is a command whose SDK return is always nonzero.
                EndPaint(hwnd, &ps);
            }
            else{
                PostQuitMessage(s_PaintFailureExitCode);
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 paint preparation failed; exit_domain=application exit_code={}"), s_PaintFailureExitCode);
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
    const bool loopResult = __hidden_win32_message_loop::RunWin32TimedFrameLoop(
        [&](){ return data<Common::WinFrame>().isActive() || m_graphics.shouldRenderUnfocused(); },
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
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = FrameDetail::s_WarningEventName, .category = FrameDetail::s_WindowLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
    if(classFailed){
        const DiagnosticEventText message = MakeDiagnosticEventText("Frame window class release failed; native_domain=Win32 native_code={}", classError);
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = FrameDetail::s_WarningEventName, .category = FrameDetail::s_WindowLifecycleCategory, .message = message.view(), .file = __FILE__, .line = __LINE__, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

