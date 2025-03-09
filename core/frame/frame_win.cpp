// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>

#include "frame.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{
    class WinFrame : public FrameData{
    public:
        inline bool& isActive(){ return reinterpret_cast<bool&>(m_data.u8[4]); }
        inline const bool& isActive()const{ return reinterpret_cast<const bool&>(m_data.u8[4]); }

        inline HINSTANCE& instance(){ return reinterpret_cast<HINSTANCE&>(m_data.ptr[1]); }
        inline const HINSTANCE& instance()const{ return reinterpret_cast<const HINSTANCE&>(m_data.ptr[1]); }

        inline HWND& hwnd(){ return reinterpret_cast<HWND&>(m_data.ptr[2]); }
        inline const HWND& hwnd()const{ return reinterpret_cast<const HWND&>(m_data.ptr[2]); }
    };


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
                    _this->data<WinFrame>().isActive() = false;
                    break;
                default:
                    _this->data<WinFrame>().isActive() = true;
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

    data<__hidden_frame::WinFrame>().width() = width;
    data<__hidden_frame::WinFrame>().height() = height;
    data<__hidden_frame::WinFrame>().isActive() = false;
    data<__hidden_frame::WinFrame>().instance() = reinterpret_cast<HINSTANCE>(inst);
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
        wc.hInstance = data<__hidden_frame::WinFrame>().instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
        wc.lpszClassName = ClassName;
    }
    if(!RegisterClassEx(&wc)){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window registration failed"));
        return false;
    }

    RECT rc = { 0, 0, static_cast<i32>(data<__hidden_frame::WinFrame>().width()), static_cast<i32>(data<__hidden_frame::WinFrame>().height()) };

    data<__hidden_frame::WinFrame>().hwnd() = CreateWindowEx(
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
    if(!data<__hidden_frame::WinFrame>().hwnd()){
        NWB_LOGGER_FATAL(NWB_TEXT("Frame window creation failed"));
        return false;
    }

    {
        if(!AdjustWindowRectEx(&rc, Style, FALSE, StyleEx)){
            data<__hidden_frame::WinFrame>().hwnd() = nullptr;
            NWB_LOGGER_FATAL(NWB_TEXT("Frame window adjustment failed"));
            return false;
        }

        const auto actualWidth = rc.right - rc.left;
        const auto actualHeight = rc.bottom - rc.top;
        const auto x = (GetSystemMetrics(SM_CXSCREEN) - actualWidth) >> 1;
        const auto y = (GetSystemMetrics(SM_CYSCREEN) - actualHeight) >> 1;

        if(!MoveWindow(data<__hidden_frame::WinFrame>().hwnd(), x, y, actualWidth, actualHeight, false)){
            data<__hidden_frame::WinFrame>().hwnd() = nullptr;
            NWB_LOGGER_FATAL(NWB_TEXT("Frame window moving failed"));
            return false;
        }
    }

    if(!startup())
        return false;

    return true;
}
bool Frame::showFrame(){
    ShowWindow(data<__hidden_frame::WinFrame>().hwnd(), SW_SHOW);
    return true;
}
bool Frame::mainLoop(){
    MSG message = {};

    std::chrono::steady_clock::time_point lateTime(std::chrono::steady_clock::now());

    for(;;){
        for(;;){
            if(data<__hidden_frame::WinFrame>().isActive()){
                if(!PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
                    break;
            }
            else
                GetMessage(&message, nullptr, 0, 0);

            if(message.message == WM_QUIT)
                return true;

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        std::chrono::steady_clock::time_point currentTime(std::chrono::steady_clock::now());
        std::chrono::duration<float, std::chrono::seconds::period> timeDifference(currentTime - lateTime);
        lateTime = currentTime;

        if(!update(timeDifference.count()))
            break;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

