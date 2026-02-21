// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <mutex>
#include <windows.h>

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WinFrame : public FrameData{
public:
    inline bool& isActive(){ return reinterpret_cast<bool&>(m_data.u8[4]); }
    inline const bool& isActive()const{ return reinterpret_cast<const bool&>(m_data.u8[4]); }

    inline HINSTANCE& instance(){ return reinterpret_cast<HINSTANCE&>(m_data.ptr[0]); }
    inline const HINSTANCE& instance()const{ return reinterpret_cast<const HINSTANCE&>(m_data.ptr[0]); }

    inline HWND& hwnd(){ return reinterpret_cast<HWND&>(m_data.ptr[1]); }
    inline const HWND& hwnd()const{ return reinterpret_cast<const HWND&>(m_data.ptr[1]); }
};


// in windows, the frame is a singleton
static Frame* g_frame = nullptr;
static HFONT g_font = nullptr;
static HWND g_listHwnd = nullptr;
static Deque<Pair<BasicString<tchar>, Log::Type>> g_messages;

static std::mutex g_listMutex;

static WNDPROC g_origListProc = nullptr;

static LRESULT CALLBACK listProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(uMsg == WM_KEYDOWN && wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
        SendMessage(GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return CallWindowProc(g_origListProc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK winProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* _this = g_frame){
        switch(uMsg){
        case WM_CREATE:
        {
            g_font = CreateFont(
                10,
                0,
                0, 0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY,
                DEFAULT_PITCH | FF_SWISS,
                NWB_TEXT("Terminal")
            );

            g_listHwnd = CreateWindowEx(
                0,
                NWB_TEXT("LISTBOX"),
                nullptr,
                WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWVARIABLE | WS_VSCROLL | LBS_NOTIFY,
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
            else{
                SendMessage(g_listHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
                g_origListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_listHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(listProc)));
            }
        }
        return 0;

        case WM_DESTROY:
        {
            if(g_font){
                DeleteObject(g_font);
                g_font = nullptr;
            }
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

        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            MoveWindow(g_listHwnd, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
        }
        return 0;

        case WM_EXITSIZEMOVE:
        {
            SendMessage(g_listHwnd, WM_SETREDRAW, FALSE, 0);
            SendMessage(g_listHwnd, LB_RESETCONTENT, 0, 0);
            for(auto& str : g_messages)
                SendMessage(g_listHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(str.first().c_str()));
            SendMessage(g_listHwnd, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_listHwnd, nullptr, TRUE);
        }
        return 0;

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
            if(mis->itemID >= 0 && mis->itemID < static_cast<decltype(mis->itemID)>(g_messages.size())){
                const auto& curStr = g_messages[static_cast<u32>(mis->itemID)];

                RECT rect;
                GetClientRect(hwnd, &rect);
                rect.right -= rect.left;
                rect.left = rect.top = rect.bottom = 0;

                HDC hdc = GetDC(g_listHwnd);
                auto* hFont = reinterpret_cast<HFONT>(SendMessage(g_listHwnd, WM_GETFONT, 0, 0));
                auto* hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));

                DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT);

                mis->itemHeight = rect.bottom - rect.top;

                SelectObject(hdc, hOldFont);
                ReleaseDC(g_listHwnd, hdc);
            }
        }
        return TRUE;

        case WM_KEYDOWN:
        {
            if(wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
                std::unique_lock lock(g_listMutex);
                if(!g_messages.empty()){
                    BasicString<tchar> combined;
                    for(const auto& msg : g_messages){
                        combined += msg.first();
                        combined += NWB_TEXT("\r\n");
                    }
                    const size_t byteSize = (combined.size() + 1) * sizeof(tchar);
                    if(OpenClipboard(hwnd)){
                        EmptyClipboard();
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
                        if(hMem){
                            memcpy(GlobalLock(hMem), combined.c_str(), byteSize);
                            GlobalUnlock(hMem);
#if defined(UNICODE) || defined(_UNICODE)
                            SetClipboardData(CF_UNICODETEXT, hMem);
#else
                            SetClipboardData(CF_TEXT, hMem);
#endif
                        }
                        CloseClipboard();
                    }
                }
            }
        }
        return 0;

        case WM_DRAWITEM:
        {
            auto* dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if(dis->itemID >= 0 && dis->itemID < static_cast<decltype(dis->itemID)>(g_messages.size())){
                const auto& curData = g_messages[static_cast<u32>(dis->itemID)];

                HDC hdc = dis->hDC;
                RECT rect = dis->rcItem;

                COLORREF textColor;
                COLORREF bgColor;
                switch(curData.second()){
                case Log::Type::Info:
                    if(dis->itemID & 1){
                        textColor = RGB(80, 80, 80);
                        bgColor = RGB(255, 255, 255);
                    }
                    else{
                        textColor = RGB(0, 0, 0);
                        bgColor = RGB(230, 230, 230);
                    }
                    break;
                case Log::Type::Warning:
                    if(dis->itemID & 1){
                        textColor = RGB(60, 60, 60);
                        bgColor = RGB(200, 200, 0);
                    }
                    else{
                        textColor = RGB(0, 0, 0);
                        bgColor = RGB(170, 170, 0);
                    }
                    break;
                case Log::Type::Error:
                    if(dis->itemID & 1){
                        textColor = RGB(255, 255, 255);
                        bgColor = RGB(200, 0, 0);
                    }
                    else{
                        textColor = RGB(200, 200, 200);
                        bgColor = RGB(170, 0, 0);
                    }
                    break;
                case Log::Type::Fatal:
                    if(dis->itemID & 1){
                        textColor = RGB(255, 255, 0);
                        bgColor = RGB(250, 0, 0);
                    }
                    else{
                        textColor = RGB(200, 200, 0);
                        bgColor = RGB(220, 0, 0);
                    }
                    break;
                default:
                    if(dis->itemID & 1){
                        textColor = RGB(80, 80, 80);
                        bgColor = RGB(255, 255, 255);
                    }
                    else{
                        textColor = RGB(0, 0, 0);
                        bgColor = RGB(230, 230, 230);
                    }
                    break;
                }

                HBRUSH hBrush = CreateSolidBrush(bgColor);
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);

                SetTextColor(hdc, textColor);
                SetBkMode(hdc, TRANSPARENT);
                DrawText(hdc, curData.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT);
            }
        }
        return TRUE;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Frame::Frame(void* inst){
    __hidden_frame::g_frame = this;

    data<__hidden_frame::WinFrame>().instance() = reinterpret_cast<HINSTANCE>(inst);
}
Frame::~Frame(){
    cleanup();

    __hidden_frame::g_messages.clear();
    __hidden_frame::g_frame = nullptr;
}

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

    Timer lateTime(TimerNow());

    for(;;){
        if(data<__hidden_frame::WinFrame>().isActive()){
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

void Frame::print(BasicStringView<tchar> str, Log::Type type){
    std::unique_lock lock(__hidden_frame::g_listMutex);

    __hidden_frame::g_messages.emplace_back(BasicString<tchar>(str), type);
    SendMessage(__hidden_frame::g_listHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(__hidden_frame::g_messages[__hidden_frame::g_messages.size() - 1].first().c_str()));

    auto numItem = SendMessage(__hidden_frame::g_listHwnd, LB_GETCOUNT, 0, 0);
    if(numItem > 0)
        SendMessage(__hidden_frame::g_listHwnd, LB_SETCURSEL, numItem - 1, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

