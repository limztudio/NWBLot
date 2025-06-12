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
    // in windows, the frame is a singleton
    static Frame* g_frame = nullptr;
    static LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
        if(auto* _this = g_frame){
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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Frame::Frame(void* inst, u16 width, u16 height){
    __hidden_frame::g_frame = this;

    data<Common::WinFrame>().width() = width;
    data<Common::WinFrame>().height() = height;
    data<Common::WinFrame>().isActive() = false;
    data<Common::WinFrame>().instance() = reinterpret_cast<HINSTANCE>(inst);
}
Frame::~Frame(){
    cleanup();
    __hidden_frame::g_frame = nullptr;
}

bool Frame::init(){
    const tchar* ClassName = NWB_TEXT("NWB_FRAME");
    const tchar* AppName = NWB_TEXT("NWBLoader");
    constexpr DWORD StyleEx = 0;
    constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;

    WNDCLASSEX wc = {};
    {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = __hidden_frame::winProc;
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
            Timer currentTime(TimerNow());
            auto timeDifference = DurationInSeconds<float>(currentTime, lateTime);
            lateTime = currentTime;

            if(!update(timeDifference))
                break;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

