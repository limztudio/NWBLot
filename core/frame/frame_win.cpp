// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>

#include "frame.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// in windows, the frame is a singleton
static Frame* g_Frame = nullptr;
static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* _this = g_Frame){
        PAINTSTRUCT ps;

        switch(uMsg){
        case WM_DESTROY:
        {
            PostQuitMessage(0);
        }
        return 0;
        case WM_CLOSE:
        {
            DestroyWindow(hwnd);
        }
        return 0;

        case WM_ACTIVATE:
        {
            switch(LOWORD(wParam)){
            case WA_INACTIVE:
                _this->data<Common::WinFrame>().isActive() = false;
                break;
            default:
                _this->data<Common::WinFrame>().isActive() = true;
                break;
            }
        }
        return 0;

        case WM_PAINT:
        {
            bool ret = false;
            if(auto hdc = BeginPaint(hwnd, &ps)){
                ret = _this->render();
                EndPaint(hwnd, &ps);
            }
            if(!ret)
                PostQuitMessage(0);
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
    const tchar* ClassName = NWB_TEXT("NWB_FRAME");
    const tchar* AppName = NWB_TEXT("NWBLoader");
    constexpr DWORD StyleEx = 0;
    constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

    WNDCLASSEX wc = {};
    {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = __hidden_frame::WinProc;
        wc.hInstance = data<Common::WinFrame>().instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
        wc.lpszClassName = ClassName;
    }
    if(!RegisterClassEx(&wc)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window registration failed"));
        return false;
    }

    RECT rc = { 0, 0, static_cast<i32>(data<Common::WinFrame>().width()), static_cast<i32>(data<Common::WinFrame>().height()) };

    data<Common::WinFrame>().hwnd() = CreateWindowEx(
        StyleEx,
        wc.lpszClassName,
        AppName,
        Style,
        0,
        0,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );
    if(!data<Common::WinFrame>().hwnd()){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window creation failed"));
        return false;
    }

    {
        if(!AdjustWindowRectEx(&rc, Style, FALSE, StyleEx)){
            data<Common::WinFrame>().hwnd() = nullptr;
            NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed"));
            return false;
        }

        const auto actualWidth = rc.right - rc.left;
        const auto actualHeight = rc.bottom - rc.top;
        const auto x = (GetSystemMetrics(SM_CXSCREEN) - actualWidth) >> 1;
        const auto y = (GetSystemMetrics(SM_CYSCREEN) - actualHeight) >> 1;

        if(!MoveWindow(data<Common::WinFrame>().hwnd(), x, y, actualWidth, actualHeight, false)){
            data<Common::WinFrame>().hwnd() = nullptr;
            NWB_LOGGER_FATAL(NWB_TEXT("Frame window moving failed"));
            return false;
        }
    }

    if(!startup())
        return false;

    return true;
}
bool Frame::showFrame(){
    ShowWindow(data<Common::WinFrame>().hwnd(), SW_SHOW);
    return true;
}
bool Frame::mainLoop(){
    MSG message = {};

    Timer lateTime(TimerNow());

    for(;;){
        if(data<Common::WinFrame>().isActive()){
            while(PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)){
                if(message.message == WM_QUIT)
                    return true;

                TranslateMessage(&message);
                DispatchMessage(&message);
            }
        }
        else{
            if(GetMessage(&message, nullptr, 0, 0) <= 0)
                return true;

            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }

        {
            RECT rect = {};
            const bool hasRect = GetClientRect(data<Common::WinFrame>().hwnd(), &rect) != FALSE;
            const bool windowVisible = hasRect && (rect.right > rect.left) && (rect.bottom > rect.top);
            const u32 width = windowVisible ? static_cast<u32>(rect.right - rect.left) : 0;
            const u32 height = windowVisible ? static_cast<u32>(rect.bottom - rect.top) : 0;
            const bool windowIsInFocus = GetForegroundWindow() == data<Common::WinFrame>().hwnd();
            m_graphics.updateWindowState(width, height, windowVisible, windowIsInFocus);

            if(auto* deviceManager = m_graphics.getDeviceManager()){
                const tchar* title = deviceManager->getWindowTitle();
                if(title && m_appliedWindowTitle != title){
                    m_appliedWindowTitle = title;
#ifdef NWB_UNICODE
                    SetWindowTextW(data<Common::WinFrame>().hwnd(), m_appliedWindowTitle.c_str());
#else
                    SetWindowTextA(data<Common::WinFrame>().hwnd(), m_appliedWindowTitle.c_str());
#endif
                }
            }

            Timer currentTime(TimerNow());
            auto timeDifference = DurationInSeconds<float>(currentTime, lateTime);
            lateTime = currentTime;

            if(!update(timeDifference))
                break;
        }
    }
    return false;
}

void Frame::setupPlatform(void* inst){
    __hidden_frame::g_Frame = this;
    data<Common::WinFrame>().isActive() = false;
    data<Common::WinFrame>().instance() = reinterpret_cast<HINSTANCE>(inst);
}
void Frame::cleanupPlatform(){
    __hidden_frame::g_Frame = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

