// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/standalone_runtime.h>
#include <global/unique_ptr.h>
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr SHORT s_ControlKeyDownMask = 0x8000;
inline constexpr WPARAM s_CopyControlKey = static_cast<WPARAM>('C');
inline constexpr COLORREF s_DefaultLogRowTextColor = RGB(0, 0, 0);
inline constexpr COLORREF s_DefaultLogRowBackgroundColor = RGB(230, 230, 230);

class WinFrame : public FrameData{
public:
    inline HINSTANCE instance()const noexcept{ return static_cast<HINSTANCE>(m_data.ptr[0]); }
    inline void setInstance(HINSTANCE value)noexcept{ m_data.ptr[0] = value; }

    inline HWND hwnd()const noexcept{ return static_cast<HWND>(m_data.ptr[1]); }
    inline void setHwnd(HWND value)noexcept{ m_data.ptr[1] = value; }
};

struct LogRowColors{
    COLORREF text = s_DefaultLogRowTextColor;
    COLORREF background = s_DefaultLogRowBackgroundColor;
};

inline constexpr LogRowColors s_DefaultEvenLogRowColors{};
inline constexpr LogRowColors s_InfoOddLogRowColors{ RGB(80, 80, 80), RGB(255, 255, 255) };
inline constexpr LogRowColors s_WarningEvenLogRowColors{ RGB(0, 0, 0), RGB(170, 170, 0) };
inline constexpr LogRowColors s_WarningOddLogRowColors{ RGB(60, 60, 60), RGB(200, 200, 0) };
inline constexpr LogRowColors s_CriticalWarningEvenLogRowColors{ RGB(240, 240, 240), RGB(170, 80, 0) };
inline constexpr LogRowColors s_CriticalWarningOddLogRowColors{ RGB(255, 255, 255), RGB(200, 100, 0) };
inline constexpr LogRowColors s_AssertEvenLogRowColors{ RGB(200, 200, 200), RGB(120, 0, 160) };
inline constexpr LogRowColors s_AssertOddLogRowColors{ RGB(255, 255, 255), RGB(145, 20, 190) };
inline constexpr LogRowColors s_ErrorEvenLogRowColors{ RGB(200, 200, 200), RGB(170, 0, 0) };
inline constexpr LogRowColors s_ErrorOddLogRowColors{ RGB(255, 255, 255), RGB(200, 0, 0) };
inline constexpr LogRowColors s_FatalEvenLogRowColors{ RGB(200, 200, 0), RGB(220, 0, 0) };
inline constexpr LogRowColors s_FatalOddLogRowColors{ RGB(255, 255, 0), RGB(250, 0, 0) };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageItem = Pair<LogString, Core::Common::LogType::Enum>;
using MessageDeque = Deque<MessageItem, LogArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MessageArenaName("logger/server/frame/messages");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep the store inside Frame so it tears down before the allocator runtime; late prints are safe no-ops.
struct MessageStore{
    MessageStore()
        : arena(s_MessageArenaName)
        , messages(arena)
    {}

    LogArena arena;
    MessageDeque messages;
};

static Frame* s_Frame = nullptr;
static HFONT s_Font = nullptr;
static HWND s_ListHwnd = nullptr;
static constexpr int s_LogFontHeight = 10;

// Points at the live Frame store; null outside Frame lifetime. Guarded by s_ListMutex.
static UniquePtr<MessageStore> s_Store;

static Futex s_ListMutex;

static WNDPROC s_OrigListProc = nullptr;

// Callers must already hold s_ListMutex; validates the item index against the live store.
static bool IsMessageIndexValid(UINT itemID)noexcept{
    return s_Store && static_cast<usize>(itemID) < s_Store->messages.size();
}

static LogRowColors SelectLogRowColors(const bool alternate, const LogRowColors& even, const LogRowColors& odd)noexcept{
    return alternate ? odd : even;
}

static LogRowColors ResolveLogRowColors(const Core::Common::LogType::Enum type, const bool alternate)noexcept{
    switch(type){
    case Core::Common::LogType::EssentialInfo:
    case Core::Common::LogType::Info:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    case Core::Common::LogType::Warning:
        return SelectLogRowColors(
            alternate,
            s_WarningEvenLogRowColors,
            s_WarningOddLogRowColors
        );
    case Core::Common::LogType::CriticalWarning:
        return SelectLogRowColors(
            alternate,
            s_CriticalWarningEvenLogRowColors,
            s_CriticalWarningOddLogRowColors
        );
    case Core::Common::LogType::Assert:
        return SelectLogRowColors(
            alternate,
            s_AssertEvenLogRowColors,
            s_AssertOddLogRowColors
        );
    case Core::Common::LogType::Error:
        return SelectLogRowColors(
            alternate,
            s_ErrorEvenLogRowColors,
            s_ErrorOddLogRowColors
        );
    case Core::Common::LogType::Fatal:
        return SelectLogRowColors(
            alternate,
            s_FatalEvenLogRowColors,
            s_FatalOddLogRowColors
        );
    default:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    }
}

static LRESULT CALLBACK ListProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(uMsg == WM_KEYDOWN && wParam == s_CopyControlKey && (GetKeyState(VK_CONTROL) & s_ControlKeyDownMask)){
        SendMessage(GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return CallWindowProc(s_OrigListProc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = s_Frame){
        switch(uMsg){
        case WM_DESTROY:
        {
            if(s_Font){
                if(!DeleteObject(s_Font))
                    NWB_TCERR << NWB_TEXT("Log server: failed to delete its font.\n");
                s_Font = nullptr;
            }
            {
                ScopedLock lock(s_ListMutex);
                s_ListHwnd = nullptr;
            }
            frame->data<WinFrame>().setHwnd(nullptr);
            PostQuitMessage(0);
        }
        return 0;

        case WM_CLOSE:
            if(!DestroyWindow(hwnd))
                NWB_TCERR << NWB_TEXT("Log server: failed to destroy its window.\n");
            return 0;

        case WM_CREATE:
        {
            s_Font = CreateFont(
                s_LogFontHeight,
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

            s_ListHwnd = CreateWindowEx(
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
            if(!s_ListHwnd)
                PostQuitMessage(0);
            else{
                SendMessage(s_ListHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(s_Font), TRUE);
                s_OrigListProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(s_ListHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc)));
            }
        }
        return 0;

        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            MoveWindow(s_ListHwnd, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE);
        }
        return 0;

        case WM_EXITSIZEMOVE:
        {
            usize count = 0u;
            {
                ScopedLock lock(s_ListMutex);
                count = s_Store ? s_Store->messages.size() : 0u;
            }
            // Re-add outside s_ListMutex: LB_ADDSTRING re-enters WM_MEASUREITEM, which takes the same non-recursive lock.
            // Handlers read the store by itemID; empty item data only triggers remeasurement.
            SendMessage(s_ListHwnd, WM_SETREDRAW, FALSE, 0);
            SendMessage(s_ListHwnd, LB_RESETCONTENT, 0, 0);
            for(usize i = 0u; i < count; ++i)
                SendMessage(s_ListHwnd, LB_ADDSTRING, 0, 0);
            SendMessage(s_ListHwnd, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(s_ListHwnd, nullptr, TRUE);
        }
        return 0;

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
            ScopedLock lock(s_ListMutex);
            if(IsMessageIndexValid(mis->itemID)){
                const auto& curStr = s_Store->messages[static_cast<usize>(mis->itemID)];

                RECT rect;
                GetClientRect(hwnd, &rect);
                rect.right -= rect.left;
                rect.left = rect.top = rect.bottom = 0;

                HDC hdc = GetDC(s_ListHwnd);
                auto* hFont = reinterpret_cast<HFONT>(SendMessage(s_ListHwnd, WM_GETFONT, 0, 0));
                auto* hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));

                DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT);

                mis->itemHeight = rect.bottom - rect.top;

                SelectObject(hdc, hOldFont);
                ReleaseDC(s_ListHwnd, hdc);
            }
        }
        return TRUE;

        case WM_KEYDOWN:
        {
            if(wParam == s_CopyControlKey && (GetKeyState(VK_CONTROL) & s_ControlKeyDownMask)){
                ScopedLock lock(s_ListMutex);
                if(s_Store && !s_Store->messages.empty()){
                    usize combinedSize = 0u;
                    for(const auto& msg : s_Store->messages){
                        const usize messageSize = msg.first().size();
                        if(messageSize > Limit<usize>::s_Max - combinedSize)
                            return 0;
                        combinedSize += messageSize;
                        if(combinedSize > Limit<usize>::s_Max - 2u)
                            return 0;
                        combinedSize += 2u;
                    }
                    if(combinedSize > (Limit<usize>::s_Max / sizeof(tchar)) - 1u)
                        return 0;

                    LogString combined{s_Store->arena};
                    combined.reserve(combinedSize);
                    for(const auto& msg : s_Store->messages){
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
                                NWB_MEMCPY(lockedMemory, byteSize, combined.data(), byteSize);
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
            ScopedLock lock(s_ListMutex);
            if(IsMessageIndexValid(dis->itemID)){
                const auto& curData = s_Store->messages[static_cast<usize>(dis->itemID)];

                HDC hdc = dis->hDC;
                RECT rect = dis->rcItem;

                const LogRowColors colors = ResolveLogRowColors(curData.second(), (dis->itemID & 1) != 0);
                HBRUSH hBrush = CreateSolidBrush(colors.background);
                FillRect(hdc, &rect, hBrush);
                DeleteObject(hBrush);

                SetTextColor(hdc, colors.text);
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
    {
        ScopedLock lock(FrameDetail::s_ListMutex);
        FrameDetail::s_Store.reset(new FrameDetail::MessageStore());
    }
    FrameDetail::s_Frame = this;

    data<FrameDetail::WinFrame>().setInstance(reinterpret_cast<HINSTANCE>(inst));
}
Frame::~Frame(){
    const HWND hwnd = data<FrameDetail::WinFrame>().hwnd();
    if(hwnd && !DestroyWindow(hwnd))
        NWB_TCERR << NWB_TEXT("Log server: failed to destroy its window during shutdown.\n");

    ScopedLock lock(FrameDetail::s_ListMutex);
    FrameDetail::s_Frame = nullptr;
    FrameDetail::s_ListHwnd = nullptr;
    FrameDetail::s_Store.reset();
}

bool Frame::init(){
    static constexpr TStringView s_WindowClassName = NWB_TEXT("NWB_LOGGER");
    static constexpr TStringView s_WindowTitle = NWB_TEXT("NWBLogger");
    constexpr DWORD s_WindowExtendedStyle = 0;
    constexpr DWORD s_WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;

    WNDCLASSEX wc = {};
    {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FrameDetail::WinProc;
        wc.hInstance = data<FrameDetail::WinFrame>().instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        // WNDCLASSEX encodes system color brushes as COLOR_* + 1.
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = s_WindowClassName.data();
    }
    if(!RegisterClassEx(&wc))
        return false;

    HWND hwnd = CreateWindowEx(
        s_WindowExtendedStyle,
        wc.lpszClassName,
        s_WindowTitle.data(),
        s_WindowStyle,
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

    return true;
}
void Frame::showFrame(){
    ShowWindow(data<FrameDetail::WinFrame>().hwnd(), SW_SHOW);
}
bool Frame::mainLoop(){
    MSG message = {};
    for(;;){
        const BOOL result = GetMessage(&message, nullptr, 0, 0);
        if(result == -1)
            return false;
        if(result == 0)
            return true;

        TranslateMessage(&message);
        DispatchMessage(&message);
    }
}

void Frame::Print(BasicStringView<tchar> str, Core::Common::LogType::Enum type){
    HWND listHwnd = nullptr;
    TStringView itemText;
    {
        ScopedLock lock(FrameDetail::s_ListMutex);

        // Worker threads may still drain after store teardown; ignore late prints.
        if(!FrameDetail::s_Store)
            return;

        FrameDetail::s_Store->messages.emplace_back(LogString(str, FrameDetail::s_Store->arena), type);
        itemText = TStringView(FrameDetail::s_Store->messages.back().first());
        listHwnd = FrameDetail::s_ListHwnd;
    }

    // SendMessage must run outside s_ListMutex: LB_ADDSTRING re-enters UI draw handlers that take the lock.
    // Handlers index by itemID; item data is unused and the single worker serializes appends.
    if(!listHwnd)
        return;

    SendMessage(listHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemText.data()));

    const auto numItem = SendMessage(listHwnd, LB_GETCOUNT, 0, 0);
    if(numItem > 0)
        SendMessage(listHwnd, LB_SETCURSEL, numItem - 1, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

