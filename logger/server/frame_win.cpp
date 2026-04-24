// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>
#include <global/win32_message_loop.h>

#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WinFrame : public FrameData{
public:
    inline bool isActive()const{ return m_data.u8[4] != 0; }
    inline void setActive(bool value){ m_data.u8[4] = value ? 1u : 0u; }

    inline HINSTANCE instance()const{ return static_cast<HINSTANCE>(m_data.ptr[0]); }
    inline void setInstance(HINSTANCE value){ m_data.ptr[0] = value; }

    inline HWND hwnd()const{ return static_cast<HWND>(m_data.ptr[1]); }
    inline void setHwnd(HWND value){ m_data.ptr[1] = value; }
};


// in windows, the frame is a singleton
static Frame* g_Frame = nullptr;
static HFONT g_Font = nullptr;
static HWND g_ListHwnd = nullptr;
static Deque<Pair<BasicString<tchar>, Log::Type::Enum>> g_Messages;

static Futex g_ListMutex;

static WNDPROC g_OrigListProc = nullptr;

static bool IsMessageIndexValid(UINT itemID){
    return static_cast<usize>(itemID) < g_Messages.size();
}

static LRESULT CALLBACK ListProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(uMsg == WM_KEYDOWN && wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
        SendMessage(GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return CallWindowProc(g_OrigListProc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = g_Frame){
        LRESULT lifecycleResult = 0;
        if(HandleWin32FrameLifecycleMessage(
            hwnd,
            uMsg,
            wParam,
            [](){
                if(g_Font){
                    DeleteObject(g_Font);
                    g_Font = nullptr;
                }
            },
            [&](const bool isActive){ frame->data<WinFrame>().setActive(isActive); },
            lifecycleResult
        ))
            return lifecycleResult;

        switch(uMsg){
        case WM_CREATE:
        {
            g_Font = CreateFont(
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

            g_ListHwnd = CreateWindowEx(
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
                frame->data<WinFrame>().instance(),
                nullptr
            );
            if(!g_ListHwnd)
                PostQuitMessage(0);
            else{
                SendMessage(g_ListHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_Font), TRUE);
                g_OrigListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(g_ListHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc)));
            }
        }
        return 0;

        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            MoveWindow(g_ListHwnd, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
        }
        return 0;

        case WM_EXITSIZEMOVE:
        {
            SendMessage(g_ListHwnd, WM_SETREDRAW, FALSE, 0);
            SendMessage(g_ListHwnd, LB_RESETCONTENT, 0, 0);
            for(auto& str : g_Messages)
                SendMessage(g_ListHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(str.first().c_str()));
            SendMessage(g_ListHwnd, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(g_ListHwnd, nullptr, TRUE);
        }
        return 0;

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
            if(IsMessageIndexValid(mis->itemID)){
                const auto& curStr = g_Messages[static_cast<usize>(mis->itemID)];

                RECT rect;
                GetClientRect(hwnd, &rect);
                rect.right -= rect.left;
                rect.left = rect.top = rect.bottom = 0;

                HDC hdc = GetDC(g_ListHwnd);
                auto* hFont = reinterpret_cast<HFONT>(SendMessage(g_ListHwnd, WM_GETFONT, 0, 0));
                auto* hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));

                DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT);

                mis->itemHeight = rect.bottom - rect.top;

                SelectObject(hdc, hOldFont);
                ReleaseDC(g_ListHwnd, hdc);
            }
        }
        return TRUE;

        case WM_KEYDOWN:
        {
            if(wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
                ScopedLock lock(g_ListMutex);
                if(!g_Messages.empty()){
                    BasicString<tchar> combined;
                    for(const auto& msg : g_Messages){
                        combined += msg.first();
                        combined += NWB_TEXT("\r\n");
                    }
                    if(combined.size() > (Limit<usize>::s_Max / sizeof(tchar)) - 1u)
                        return 0;

                    const usize byteSize = (combined.size() + 1) * sizeof(tchar);
                    if(OpenClipboard(hwnd)){
                        EmptyClipboard();
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize);
                        if(hMem){
                            void* lockedMemory = GlobalLock(hMem);
                            if(lockedMemory){
                                NWB_MEMCPY(lockedMemory, byteSize, combined.c_str(), byteSize);
                                GlobalUnlock(hMem);
#if defined(UNICODE) || defined(_UNICODE)
                                if(!SetClipboardData(CF_UNICODETEXT, hMem))
#else
                                if(!SetClipboardData(CF_TEXT, hMem))
#endif
                                    GlobalFree(hMem);
                            }
                            else
                                GlobalFree(hMem);
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
            if(IsMessageIndexValid(dis->itemID)){
                const auto& curData = g_Messages[static_cast<usize>(dis->itemID)];

                HDC hdc = dis->hDC;
                RECT rect = dis->rcItem;

                COLORREF textColor;
                COLORREF bgColor;
                switch(curData.second()){
                case Log::Type::EssentialInfo:
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
                case Log::Type::CriticalWarning:
                    if(dis->itemID & 1){
                        textColor = RGB(255, 255, 255);
                        bgColor = RGB(200, 100, 0);
                    }
                    else{
                        textColor = RGB(240, 240, 240);
                        bgColor = RGB(170, 80, 0);
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
    FrameDetail::g_Frame = this;

    data<FrameDetail::WinFrame>().setInstance(reinterpret_cast<HINSTANCE>(inst));
}
Frame::~Frame(){
    cleanup();

    FrameDetail::g_Messages.clear();
    FrameDetail::g_Frame = nullptr;
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
        wc.lpfnWndProc = FrameDetail::WinProc;
        wc.hInstance = data<FrameDetail::WinFrame>().instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
        wc.lpszClassName = ClassName;
    }
    if(!RegisterClassEx(&wc))
        return false;

    HWND hwnd = CreateWindowEx(
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
    data<FrameDetail::WinFrame>().setHwnd(hwnd);
    if(!data<FrameDetail::WinFrame>().hwnd())
        return false;

    if(!startup())
        return false;

    return true;
}
bool Frame::showFrame(){
    ShowWindow(data<FrameDetail::WinFrame>().hwnd(), SW_SHOW);
    return true;
}
bool Frame::mainLoop(){
    Timer lateTime(TimerNow());

    for(;;){
        switch(PumpWin32FrameMessages([&](){ return data<FrameDetail::WinFrame>().isActive(); })){
        case Win32MessagePumpResult::Quit:
            return true;
        case Win32MessagePumpResult::SkipUpdate:
            continue;
        case Win32MessagePumpResult::Continue:
            break;
        }

        {
            const f32 timeDifference = ConsumeTimerDeltaSeconds<f32>(lateTime);

            if(!update(timeDifference))
                break;
        }
    }
    return false;
}

void Frame::print(BasicStringView<tchar> str, Log::Type::Enum type){
    ScopedLock lock(FrameDetail::g_ListMutex);

    FrameDetail::g_Messages.emplace_back(BasicString<tchar>(str), type);
    SendMessage(FrameDetail::g_ListHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(FrameDetail::g_Messages[FrameDetail::g_Messages.size() - 1].first().c_str()));

    auto numItem = SendMessage(FrameDetail::g_ListHwnd, LB_GETCOUNT, 0, 0);
    if(numItem > 0)
        SendMessage(FrameDetail::g_ListHwnd, LB_SETCURSEL, numItem - 1, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

