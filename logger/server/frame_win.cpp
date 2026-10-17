// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame.h"
#include "clipboard_text_internal.h"

#include <core/alloc/standalone_runtime.h>

#include <global/diagnostics.h>
#include <global/expected.h>
#include <global/scope_exit.h>
#include <global/unique_ptr.h>


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
inline constexpr UINT s_SynchronizeMessageRows = WM_APP + 1u;
inline constexpr COLORREF s_DefaultLogRowTextColor = RGB(0, 0, 0);
inline constexpr COLORREF s_DefaultLogRowBackgroundColor = RGB(230, 230, 230);

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


inline constexpr Name s_MessageArenaName("logger/server/frame/messages");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Keep the store inside Frame so it tears down before the allocator runtime; late prints are safe no-ops.
struct MessageStore{
    LogArena arena;
    MessageDeque messages;


public:
    MessageStore()
        : arena(s_MessageArenaName)
        , messages(arena)
    {}
};

static Frame* s_Frame = nullptr;
static HFONT s_Font = nullptr;
static HWND s_ListHwnd = nullptr;
static constexpr int s_LogFontHeight = 10;

// Points at the live Frame store; null outside Frame lifetime. Guarded by s_ListMutex.
static UniquePtr<MessageStore> s_Store;

static Futex s_ListMutex;

static WNDPROC s_OrigListProc = nullptr;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ReleaseLogFont()noexcept{
    if(!s_Font)
        return;
    if(!DeleteObject(s_Font))
        CaptureDiagnosticEvent("logger_window_lifecycle", "Log server: failed to delete its font", __FILE__, __LINE__);
    s_Font = nullptr;
}

// This runs on the UI thread; native row queries and additions cannot interleave with a resize rebuild.
[[nodiscard]] static Expected<void> SynchronizeMessageRows(const HWND listHwnd)noexcept{
    usize messageCount = 0u;
    {
        NothrowScopedLock lock(s_ListMutex);
        messageCount = s_Store ? s_Store->messages.size() : 0u;
    }
    const LRESULT existingCount = SendMessage(listHwnd, LB_GETCOUNT, 0, 0);
    if(existingCount == LB_ERR || static_cast<usize>(existingCount) > messageCount)
        return MakeUnexpected(Failure{});
    for(usize index = static_cast<usize>(existingCount); index < messageCount; ++index){
        const LRESULT added = SendMessage(listHwnd, LB_ADDSTRING, 0, 0);
        if(added == LB_ERR || added == LB_ERRSPACE)
            return MakeUnexpected(Failure{});
    }
    if(messageCount != 0u && SendMessage(listHwnd, LB_SETCURSEL, messageCount - 1u, 0) == LB_ERR)
        return MakeUnexpected(Failure{});
    return {};
}

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
        return SelectLogRowColors(alternate, s_WarningEvenLogRowColors, s_WarningOddLogRowColors);
    case Core::Common::LogType::CriticalWarning:
        return SelectLogRowColors(alternate, s_CriticalWarningEvenLogRowColors, s_CriticalWarningOddLogRowColors);
    case Core::Common::LogType::Assert:
        return SelectLogRowColors(alternate, s_AssertEvenLogRowColors, s_AssertOddLogRowColors);
    case Core::Common::LogType::Error:
        return SelectLogRowColors(alternate, s_ErrorEvenLogRowColors, s_ErrorOddLogRowColors);
    case Core::Common::LogType::Fatal:
        return SelectLogRowColors(alternate, s_FatalEvenLogRowColors, s_FatalOddLogRowColors);
    default:
        return SelectLogRowColors(alternate, s_DefaultEvenLogRowColors, s_InfoOddLogRowColors);
    }
}

static void CopyMessagesToClipboard(const HWND hwnd)noexcept{
    Optional<ClipboardText> memory;
    Optional<ClipboardTextFailure> buildFailure;
    {
        NothrowScopedLock lock(s_ListMutex);
        if(!s_Store || s_Store->messages.empty())
            return;
        auto built = BuildClipboardText(s_Store->messages);
        if(!built)
            buildFailure.emplace(Move(built.error()));
        else
            memory.emplace(Move(*built));
    }
    if(buildFailure){
        ClipboardTextFailure& failure = *buildFailure;
        Expected<void, ErrorCode> cleanup;
        if(failure.memory)
            cleanup = failure.memory->close();
        const DiagnosticEventText detail = cleanup
            ? MakeDiagnosticEventText("{}: native_category={} native_code={}", failure.operation, failure.code.category().name(), failure.code.value())
            : MakeDiagnosticEventText("{}: native_category={} native_code={}; cleanup_category={} cleanup_code={}", failure.operation, failure.code.category().name(), failure.code.value(), cleanup.error().category().name(), cleanup.error().value());
        CaptureDiagnosticEvent("logger_clipboard", detail.view(), __FILE__, __LINE__);
        return;
    }

    if(!OpenClipboard(hwnd)){
        CaptureDiagnosticEvent("logger_clipboard", "Failed to open clipboard", __FILE__, __LINE__);
        return;
    }
    ScopeExit closeClipboard([]()noexcept{
        if(!CloseClipboard())
            CaptureDiagnosticEvent("logger_clipboard", "Failed to close clipboard", __FILE__, __LINE__);
    }
    );
    if(!EmptyClipboard()){
        CaptureDiagnosticEvent("logger_clipboard", "Failed to empty clipboard", __FILE__, __LINE__);
        return;
    }
#if defined(UNICODE) || defined(_UNICODE)
    const HANDLE transferred = SetClipboardData(CF_UNICODETEXT, memory->native());
#else
    const HANDLE transferred = SetClipboardData(CF_TEXT, memory->native());
#endif
    if(!transferred){
        CaptureDiagnosticEvent("logger_clipboard", "Failed to publish clipboard text", __FILE__, __LINE__);
        return;
    }
    memory->release();
}

static LRESULT CALLBACK ListProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
    if(uMsg == s_SynchronizeMessageRows){
        const auto synchronized = SynchronizeMessageRows(hwnd);
        if(!synchronized){
            CaptureDiagnosticEvent("logger_window_messages", "Log server: failed to synchronize its displayed log rows", __FILE__, __LINE__);
            return FALSE;
        }
        return TRUE;
    }
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
            ReleaseLogFont();
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
                CaptureDiagnosticEvent("logger_window_lifecycle", "Log server: failed to destroy its window", __FILE__, __LINE__);
            return 0;

        case WM_CREATE:
        {
            s_Font = CreateFont(s_LogFontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, NWB_TEXT("Terminal"));
            if(!s_Font)
                CaptureDiagnosticEvent("logger_window_lifecycle", "Log server: failed to create its font; using the system font", __FILE__, __LINE__);

            const HWND listHwnd = CreateWindowEx(0, NWB_TEXT("LISTBOX"), nullptr, WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWVARIABLE | WS_VSCROLL | LBS_NOTIFY, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(1), frame->data<WinFrame>().instance(), nullptr);
            if(!listHwnd){
                CaptureDiagnosticEvent("logger_window_lifecycle", "Log server: failed to create its message list", __FILE__, __LINE__);
                return -1;
            }
            SendMessage(listHwnd, WM_SETFONT, reinterpret_cast<WPARAM>(s_Font), TRUE);
            const LONG_PTR originalProc = SetWindowLongPtr(listHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc));
            if(originalProc == 0){
                CaptureDiagnosticEvent("logger_window_lifecycle", "Log server: failed to subclass its message list", __FILE__, __LINE__);
                return -1;
            }
            s_OrigListProc = reinterpret_cast<WNDPROC>(originalProc);
            {
                ScopedLock lock(s_ListMutex);
                s_ListHwnd = listHwnd;
            }
            const auto synchronized = SynchronizeMessageRows(listHwnd);
            if(!synchronized){
                CaptureDiagnosticEvent("logger_window_messages", "Log server: failed to initialize its displayed log rows", __FILE__, __LINE__);
                return -1;
            }
        }
        return 0;

        case WM_SIZE:
        {
            if(!s_ListHwnd)
                return 0;
            RECT clientRect = {};
            if(!GetClientRect(hwnd, &clientRect)){
                CaptureDiagnosticEvent("logger_window_layout", "Log server: failed to query its client rectangle", __FILE__, __LINE__);
                return 0;
            }
            if(!MoveWindow(s_ListHwnd, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, TRUE))
                CaptureDiagnosticEvent("logger_window_layout", "Log server: failed to resize its message list", __FILE__, __LINE__);
        }
        return 0;

        case WM_EXITSIZEMOVE:
        {
            const HWND listHwnd = s_ListHwnd;
            if(!listHwnd)
                return 0;
            SendMessage(listHwnd, WM_SETREDRAW, FALSE, 0);
            ScopeExit restoreRedraw([listHwnd]()noexcept{
                SendMessage(listHwnd, WM_SETREDRAW, TRUE, 0);
                if(!InvalidateRect(listHwnd, nullptr, TRUE))
                    CaptureDiagnosticEvent("logger_window_layout", "Log server: failed to invalidate its message list", __FILE__, __LINE__);
            }
            );

            SendMessage(listHwnd, LB_RESETCONTENT, 0, 0);
            const auto synchronized = SynchronizeMessageRows(listHwnd);
            if(!synchronized)
                CaptureDiagnosticEvent("logger_window_messages", "Log server: failed to rebuild its displayed log rows", __FILE__, __LINE__);
        }
        return 0;

        case WM_MEASUREITEM:
        {
            auto* mis = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
            ScopedLock lock(s_ListMutex);
            if(IsMessageIndexValid(mis->itemID)){
                const auto& curStr = s_Store->messages[static_cast<usize>(mis->itemID)];

                mis->itemHeight = static_cast<UINT>(s_LogFontHeight);
                RECT rect = {};
                if(!GetClientRect(hwnd, &rect)){
                    CaptureDiagnosticEvent("logger_window_layout", "Log server: failed to query its text measurement rectangle", __FILE__, __LINE__);
                    return TRUE;
                }
                rect.right -= rect.left;
                rect.left = rect.top = rect.bottom = 0;

                const HDC hdc = GetDC(s_ListHwnd);
                if(!hdc){
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to acquire its text measurement device context", __FILE__, __LINE__);
                    return TRUE;
                }
                ScopeExit releaseDc([&]()noexcept{
                    if(!ReleaseDC(s_ListHwnd, hdc))
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to release its text measurement device context", __FILE__, __LINE__);
                }
                );

                const HFONT font = reinterpret_cast<HFONT>(SendMessage(s_ListHwnd, WM_GETFONT, 0, 0));
                HGDIOBJ oldFont = nullptr;
                if(font){
                    oldFont = SelectObject(hdc, font);
                    if(!oldFont || oldFont == HGDI_ERROR){
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to select its text measurement font", __FILE__, __LINE__);
                        return TRUE;
                    }
                }
                ScopeExit restoreFont([&]()noexcept{
                    if(oldFont){
                        const HGDIOBJ restored = SelectObject(hdc, oldFont);
                        if(!restored || restored == HGDI_ERROR)
                            CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to restore its text measurement font", __FILE__, __LINE__);
                    }
                }
                );

                if(!curStr.first().empty()){
                    if(DrawText(hdc, curStr.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT | DT_CALCRECT) == 0)
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to measure its log text", __FILE__, __LINE__);
                    else
                        mis->itemHeight = static_cast<UINT>(rect.bottom - rect.top);
                }
            }
        }
        return TRUE;

        case WM_KEYDOWN:
            if(wParam == s_CopyControlKey && (GetKeyState(VK_CONTROL) & s_ControlKeyDownMask))
                CopyMessagesToClipboard(hwnd);
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
                const HBRUSH brush = CreateSolidBrush(colors.background);
                if(!brush){
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to create its row background brush", __FILE__, __LINE__);
                    return TRUE;
                }
                ScopeExit releaseBrush([&]()noexcept{
                    if(!DeleteObject(brush))
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to delete its row background brush", __FILE__, __LINE__);
                }
                );

                if(!FillRect(hdc, &rect, brush))
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to fill its row background", __FILE__, __LINE__);
                const COLORREF oldTextColor = SetTextColor(hdc, colors.text);
                if(oldTextColor == CLR_INVALID){
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to set its log text color", __FILE__, __LINE__);
                    return TRUE;
                }
                ScopeExit restoreTextColor([&]()noexcept{
                    if(SetTextColor(hdc, oldTextColor) == CLR_INVALID)
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to restore its log text color", __FILE__, __LINE__);
                }
                );

                const int oldBackgroundMode = SetBkMode(hdc, TRANSPARENT);
                if(oldBackgroundMode == 0){
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to set its log text background mode", __FILE__, __LINE__);
                    return TRUE;
                }
                ScopeExit restoreBackgroundMode([&]()noexcept{
                    if(!SetBkMode(hdc, oldBackgroundMode))
                        CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to restore its log text background mode", __FILE__, __LINE__);
                }
                );

                if(!curData.first().empty() && DrawText(hdc, curData.first().c_str(), -1, &rect, DT_WORDBREAK | DT_LEFT) == 0)
                    CaptureDiagnosticEvent("logger_window_paint", "Log server: failed to draw its log text", __FILE__, __LINE__);
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


void Frame::Print(BasicStringView<tchar> str, Core::Common::LogType::Enum type){
    HWND listHwnd = nullptr;
    {
        ScopedLock lock(FrameDetail::s_ListMutex);

        // Worker threads may still drain after store teardown; ignore late prints.
        if(!FrameDetail::s_Store)
            return;

        FrameDetail::s_Store->messages.emplace_back(LogString(str, FrameDetail::s_Store->arena), type);
        listHwnd = FrameDetail::s_ListHwnd;
    }

    // SendMessage must run outside s_ListMutex: LB_ADDSTRING re-enters UI draw handlers that take the lock.
    // Handlers index by itemID; item data is unused and the single worker serializes appends.
    if(!listHwnd)
        return;

    if(!SendMessage(listHwnd, FrameDetail::s_SynchronizeMessageRows, 0, 0)){
        ScopedLock lock(FrameDetail::s_ListMutex);
        if(FrameDetail::s_ListHwnd == listHwnd)
            CaptureDiagnosticEvent("logger_window_messages", "Log server: failed to publish its displayed log rows", __FILE__, __LINE__);
    }
}

Frame::Frame(void* inst){
    {
        ScopedLock lock(FrameDetail::s_ListMutex);
        FrameDetail::s_Store = MakeUnique<FrameDetail::MessageStore>();
    }
    FrameDetail::s_Frame = this;

    data<FrameDetail::WinFrame>().setInstance(reinterpret_cast<HINSTANCE>(inst));
}

Frame::~Frame()noexcept{
    const HWND hwnd = data<FrameDetail::WinFrame>().hwnd();
    if(hwnd && !DestroyWindow(hwnd))
        CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = "warning", .category = "logger_window_lifecycle", .message = "Log server: failed to destroy its window during shutdown", .file = __FILE__, .line = __LINE__, });

    FrameDetail::ReleaseLogFont();

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

    HWND hwnd = CreateWindowEx(s_WindowExtendedStyle, wc.lpszClassName, s_WindowTitle.data(), s_WindowStyle, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, nullptr);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

