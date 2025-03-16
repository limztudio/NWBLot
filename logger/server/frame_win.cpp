// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <mutex>
#include <windows.h>

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{
    class WinFrame : public FrameData{
    public:
        inline HINSTANCE& instance(){ return reinterpret_cast<HINSTANCE&>(m_data.ptr[0]); }
        inline const HINSTANCE& instance()const{ return reinterpret_cast<const HINSTANCE&>(m_data.ptr[0]); }

        inline HWND& hwnd(){ return reinterpret_cast<HWND&>(m_data.ptr[1]); }
        inline const HWND& hwnd()const{ return reinterpret_cast<const HWND&>(m_data.ptr[1]); }
    };


    // in windows, the frame is a singleton
    static Frame* g_frame = nullptr;
    static HWND g_listHwnd = nullptr;
    static std::mutex g_listMutex;
    static LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
        if(auto* _this = g_frame){
            switch(uMsg){
            case WM_CREATE:
            {
                g_listHwnd = CreateWindowEx(
                    0,
                    NWB_TEXT("LISTBOX"),
                    nullptr,
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
                    0,
                    0,
                    0,
                    0,
                    hwnd,
                    reinterpret_cast<HMENU>(1),
                    _this->data<WinFrame>().instance(),
                    nullptr
                );
                if(!g_listHwnd)
                    PostQuitMessage(0);
            }
            return 0;

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

            case WM_SIZE:
            {
                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                MoveWindow(g_listHwnd, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
            }
            return 0;
            }
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Frame::Frame(void* inst){
    __hidden_frame::g_frame = this;

    data<__hidden_frame::WinFrame>().instance() = reinterpret_cast<HINSTANCE>(inst);
}
Frame::~Frame(){}

bool Frame::init(){
    const tchar* ClassName = NWB_TEXT("NWB_LOGGER");
    const tchar* AppName = NWB_TEXT("NWBLogger");
    constexpr DWORD StyleEx = 0;
    constexpr DWORD Style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;

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
    if(!RegisterClassEx(&wc))
        return false;

    data<__hidden_frame::WinFrame>().hwnd() = CreateWindowEx(
        StyleEx,
        wc.lpszClassName,
        AppName,
        Style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );
    if(!data<__hidden_frame::WinFrame>().hwnd())
        return false;

    return true;
}
bool Frame::showFrame(){
    ShowWindow(data<__hidden_frame::WinFrame>().hwnd(), SW_SHOW);
    return true;
}
bool Frame::mainLoop(){
    MSG message = {};
    while(GetMessage(&message, nullptr, 0, 0)){
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    return true;
}

void Frame::print(BasicStringView<tchar> str){
    std::unique_lock lock(__hidden_frame::g_listMutex);

    SendMessage(__hidden_frame::g_listHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(str.data()));

    auto numItem = SendMessage(__hidden_frame::g_listHwnd, LB_GETCOUNT, 0, 0);
    if(numItem > 0)
        SendMessage(__hidden_frame::g_listHwnd, LB_SETCURSEL, numItem - 1, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

