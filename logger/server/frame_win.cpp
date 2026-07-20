#include <core/alloc/standalone_runtime.h>
#include "frame.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>
#include <global/win32_message_loop.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_WinFrameActiveFlagByteIndex = 4u;
inline constexpr COLORREF s_DefaultLogRowTextColor = RGB(0, 0, 0);
inline constexpr COLORREF s_DefaultLogRowBackgroundColor = RGB(230, 230, 230);

class WinFrame : public FrameData{
public:
    inline bool isActive()const{ return m_data.u8[s_WinFrameActiveFlagByteIndex] != 0; }
    inline void setActive(bool value){ m_data.u8[s_WinFrameActiveFlagByteIndex] = value ? 1u : 0u; }

    inline HINSTANCE instance()const{ return static_cast<HINSTANCE>(m_data.ptr[0]); }
    inline void setInstance(HINSTANCE value){ m_data.ptr[0] = value; }

    inline HWND hwnd()const{ return static_cast<HWND>(m_data.ptr[1]); }
    inline void setHwnd(HWND value){ m_data.ptr[1] = value; }
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


using MessageItem = Pair<LogString, Log::Type::Enum>;
using MessageDeque = Deque<MessageItem, LogArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MessageArenaName("logger/server/frame/messages");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The message store (arena + deque) is arena-backed heap memory, so its lifetime MUST end while the
// standalone allocator runtime (oneTBB scalable_malloc, behind GlobalArena) is still alive. It therefore
// lives inside the Frame instance, NOT at namespace scope: a namespace-scope arena/deque would be torn
// down by the CRT atexit chain AFTER the runtime has already shut down, freeing memory into a dead
// allocator (observed as an access violation / abort on close). MessageStore is owned by the Frame and
// reached through the s_Store pointer; all access is guarded by s_ListMutex plus a live-store check, so
// a stray Frame::print from a not-yet-stopped worker thread after teardown is a safe no-op.
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

// Raw pointer -> trivially destructible, so it is safe as a namespace-scope static. Points at the live
// Frame store while a Frame exists; null before construction and after ~Frame. Guarded by s_ListMutex.
static MessageStore* s_Store = nullptr;

static Futex s_ListMutex;

static WNDPROC s_OrigListProc = nullptr;

// Callers must already hold s_ListMutex; validates the item index against the live store.
static bool IsMessageIndexValid(UINT itemID){
    return s_Store && static_cast<usize>(itemID) < s_Store->messages.size();
}

static LogRowColors SelectLogRowColors(const bool alternate, const LogRowColors& even, const LogRowColors& odd){
    return alternate ? odd : even;
}

static LogRowColors ResolveLogRowColors(const Log::Type::Enum type, const bool alternate){
    switch(type){
    case Log::Type::EssentialInfo:
    case Log::Type::Info:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    case Log::Type::Warning:
        return SelectLogRowColors(
            alternate,
            s_WarningEvenLogRowColors,
            s_WarningOddLogRowColors
        );
    case Log::Type::CriticalWarning:
        return SelectLogRowColors(
            alternate,
            s_CriticalWarningEvenLogRowColors,
            s_CriticalWarningOddLogRowColors
        );
    case Log::Type::Assert:
        return SelectLogRowColors(
            alternate,
            s_AssertEvenLogRowColors,
            s_AssertOddLogRowColors
        );
    case Log::Type::Error:
        return SelectLogRowColors(
            alternate,
            s_ErrorEvenLogRowColors,
            s_ErrorOddLogRowColors
        );
    case Log::Type::Fatal:
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
    if(uMsg == WM_KEYDOWN && wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
        SendMessage(GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return CallWindowProc(s_OrigListProc, hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK WinProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(auto* frame = s_Frame){
        LRESULT lifecycleResult = 0;
        if(
            ::HandleWin32FrameLifecycleMessage(
            hwnd,
            uMsg,
            wParam,
            [](){
                if(s_Font){
                    DeleteObject(s_Font);
                    s_Font = nullptr;
                }
            },
            [&](const bool isActive){ frame->data<WinFrame>().setActive(isActive); },
            lifecycleResult
            )
        )
            return lifecycleResult;

        switch(uMsg){
        case WM_CREATE:
        {
            s_Font = CreateFont(
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
            // Re-add the items OUTSIDE the lock. Each LB_ADDSTRING re-enters WM_MEASUREITEM on this same UI thread,
            // which locks s_ListMutex; holding it here would self-deadlock (Futex is non-recursive). The owner-draw
            // handlers index the store by itemID, so the LB_ADDSTRING item data is unused -- re-adding `count` empty
            // items just re-triggers the per-item measure over the (unchanged) store.
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
            if(wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)){
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
        FrameDetail::s_Store = new FrameDetail::MessageStore();
    }
    FrameDetail::s_Frame = this;

    data<FrameDetail::WinFrame>().setInstance(reinterpret_cast<HINSTANCE>(inst));
}
Frame::~Frame(){
    // Destroy the window first (stops UI-thread WinProc access to the store), then release the store
    // while the allocator runtime is still alive. Clearing s_Frame/s_Store under the lock makes any
    // concurrent Frame::print from a worker thread that has not yet been stopped a safe no-op.
    cleanup();

    FrameDetail::s_Frame = nullptr;

    ScopedLock lock(FrameDetail::s_ListMutex);
    delete FrameDetail::s_Store;
    FrameDetail::s_Store = nullptr;
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
        // WNDCLASSEX encodes system color brushes as COLOR_* + 1.
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
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
    return ::RunWin32TimedFrameLoop(
        [&](){ return data<FrameDetail::WinFrame>().isActive(); },
        [](){},
        [&](const f32 timeDifference){ return update(timeDifference); }
    );
}

void Frame::print(BasicStringView<tchar> str, Log::Type::Enum type){
    HWND listHwnd = nullptr;
    const tchar* itemText = nullptr;
    {
        ScopedLock lock(FrameDetail::s_ListMutex);

        // The store is released in ~Frame() while worker threads may still be draining; ignore late prints
        // rather than resurrect a torn-down store.
        if(!FrameDetail::s_Store)
            return;

        FrameDetail::s_Store->messages.emplace_back(LogString(str, FrameDetail::s_Store->arena), type);
        itemText = FrameDetail::s_Store->messages.back().first().c_str();
        listHwnd = FrameDetail::s_ListHwnd;
    }

    // CRITICAL: the SendMessage calls run OUTSIDE s_ListMutex. print() runs on the Server's worker thread, while the
    // listbox lives on the UI thread. LB_ADDSTRING on an LBS_OWNERDRAWVARIABLE listbox SYNCHRONOUSLY re-enters the UI
    // thread's WM_MEASUREITEM / WM_DRAWITEM handlers, and those lock s_ListMutex to read the store. Holding the mutex
    // across the cross-thread SendMessage would deadlock the worker (blocked in SendMessage) against the UI thread
    // (blocked on the mutex) -- the first log line would freeze the whole logserver (blank, unresponsive to WM_CLOSE).
    // The message was already appended under the lock above; the owner-draw handlers index the store by itemID (not the
    // item pointer), so the LB_ADDSTRING item data is unused and the single worker never mutates the store concurrently.
    if(!listHwnd)
        return;

    SendMessage(listHwnd, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(itemText));

    const auto numItem = SendMessage(listHwnd, LB_GETCOUNT, 0, 0);
    if(numItem > 0)
        SendMessage(listHwnd, LB_SETCURSEL, numItem - 1, 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

