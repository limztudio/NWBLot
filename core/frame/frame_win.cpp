// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>

#include "frame.h"
#include "frame_input_helpers.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// in windows, the frame is a singleton
static Frame* g_Frame = nullptr;


static bool IsExtendedKey(LPARAM lParam){
    return (static_cast<usize>(lParam) & (static_cast<usize>(1) << 24)) != 0;
}

static i32 TranslateScancode(LPARAM lParam){
    return static_cast<i32>((static_cast<usize>(lParam) >> 16) & 0x1ffu);
}

static i32 TranslateModifiers(){
    i32 mods = 0;

    if(GetKeyState(VK_SHIFT) & 0x8000)
        mods |= InputModifier::Shift;
    if(GetKeyState(VK_CONTROL) & 0x8000)
        mods |= InputModifier::Control;
    if(GetKeyState(VK_MENU) & 0x8000)
        mods |= InputModifier::Alt;
    if((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= InputModifier::Super;
    if(GetKeyState(VK_CAPITAL) & 0x0001)
        mods |= InputModifier::CapsLock;
    if(GetKeyState(VK_NUMLOCK) & 0x0001)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslateShiftKey(LPARAM lParam){
    const UINT scancode = static_cast<UINT>((static_cast<usize>(lParam) >> 16) & 0xffu);
    switch(MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX)){
    case VK_RSHIFT:
        return Key::RightShift;
    case VK_LSHIFT:
    default:
        return Key::LeftShift;
    }
}

static i32 TranslateNavigationKey(i32 extendedKey, i32 keypadKey, LPARAM lParam){
    return IsExtendedKey(lParam) ? extendedKey : keypadKey;
}

static i32 TranslateKey(WPARAM wParam, LPARAM lParam){
    const u32 vk = static_cast<u32>(wParam);

    if(vk >= '0' && vk <= '9')
        return static_cast<i32>(Key::Number0 + (vk - '0'));

    if(vk >= 'A' && vk <= 'Z')
        return static_cast<i32>(Key::A + (vk - 'A'));

    if(vk >= VK_F1 && vk <= VK_F24)
        return static_cast<i32>(Key::F1 + (vk - VK_F1));

    if(vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return static_cast<i32>(Key::Keypad0 + (vk - VK_NUMPAD0));

    switch(vk){
    case VK_SPACE: return Key::Space;
    case VK_OEM_7: return Key::Apostrophe;
    case VK_OEM_COMMA: return Key::Comma;
    case VK_OEM_MINUS: return Key::Minus;
    case VK_OEM_PERIOD: return Key::Period;
    case VK_OEM_2: return Key::Slash;
    case VK_OEM_1: return Key::Semicolon;
    case VK_OEM_PLUS: return Key::Equal;
    case VK_OEM_4: return Key::LeftBracket;
    case VK_OEM_5: return Key::Backslash;
    case VK_OEM_6: return Key::RightBracket;
    case VK_OEM_3: return Key::GraveAccent;
    case VK_OEM_102: return Key::World2;

    case VK_ESCAPE: return Key::Escape;
    case VK_RETURN: return IsExtendedKey(lParam) ? Key::KeypadEnter : Key::Enter;
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_INSERT: return TranslateNavigationKey(Key::Insert, Key::Keypad0, lParam);
    case VK_DELETE: return TranslateNavigationKey(Key::Delete, Key::KeypadDecimal, lParam);
    case VK_RIGHT: return TranslateNavigationKey(Key::Right, Key::Keypad6, lParam);
    case VK_LEFT: return TranslateNavigationKey(Key::Left, Key::Keypad4, lParam);
    case VK_DOWN: return TranslateNavigationKey(Key::Down, Key::Keypad2, lParam);
    case VK_UP: return TranslateNavigationKey(Key::Up, Key::Keypad8, lParam);
    case VK_PRIOR: return TranslateNavigationKey(Key::PageUp, Key::Keypad9, lParam);
    case VK_NEXT: return TranslateNavigationKey(Key::PageDown, Key::Keypad3, lParam);
    case VK_HOME: return TranslateNavigationKey(Key::Home, Key::Keypad7, lParam);
    case VK_END: return TranslateNavigationKey(Key::End, Key::Keypad1, lParam);
    case VK_CLEAR: return Key::Keypad5;
    case VK_CAPITAL: return Key::CapsLock;
    case VK_SCROLL: return Key::ScrollLock;
    case VK_NUMLOCK: return Key::NumLock;
    case VK_SNAPSHOT: return Key::PrintScreen;
    case VK_PAUSE: return Key::Pause;

    case VK_DECIMAL: return Key::KeypadDecimal;
    case VK_DIVIDE: return Key::KeypadDivide;
    case VK_MULTIPLY: return Key::KeypadMultiply;
    case VK_SUBTRACT: return Key::KeypadSubtract;
    case VK_ADD: return Key::KeypadAdd;
#ifdef VK_OEM_NEC_EQUAL
    case VK_OEM_NEC_EQUAL: return Key::KeypadEqual;
#endif

    case VK_SHIFT: return TranslateShiftKey(lParam);
    case VK_CONTROL: return IsExtendedKey(lParam) ? Key::RightControl : Key::LeftControl;
    case VK_MENU: return IsExtendedKey(lParam) ? Key::RightAlt : Key::LeftAlt;
    case VK_LSHIFT: return Key::LeftShift;
    case VK_RSHIFT: return Key::RightShift;
    case VK_LCONTROL: return Key::LeftControl;
    case VK_RCONTROL: return Key::RightControl;
    case VK_LMENU: return Key::LeftAlt;
    case VK_RMENU: return Key::RightAlt;
    case VK_LWIN: return Key::LeftSuper;
    case VK_RWIN: return Key::RightSuper;
    case VK_APPS: return Key::Menu;
    default:
        return Key::Unknown;
    }
}

static i32 SignedLowWord(LPARAM value){
    return static_cast<i32>(static_cast<i16>(static_cast<u16>(static_cast<usize>(value) & 0xffffu)));
}

static i32 SignedHighWord(LPARAM value){
    return static_cast<i32>(static_cast<i16>(static_cast<u16>((static_cast<usize>(value) >> 16) & 0xffffu)));
}

static i32 TranslateMouseButton(UINT message, WPARAM wParam){
    switch(message){
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return MouseButton::Left;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return MouseButton::Right;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return MouseButton::Middle;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        switch(HIWORD(wParam)){
        case XBUTTON1:
            return MouseButton::Button4;
        case XBUTTON2:
            return MouseButton::Button5;
        default:
            return -1;
        }
    default:
        return -1;
    }
}

static bool IsTextInputCodePoint(u32 unicode){
    if(unicode < 32u || unicode == 127u)
        return false;
    if(unicode > 0x10ffffu)
        return false;
    if(unicode >= 0xd800u && unicode <= 0xdfffu)
        return false;
    return true;
}

static void DispatchUnicodeInput(Frame& frame, u32 unicode){
    if(IsTextInputCodePoint(unicode))
        frame.input().keyboardCharInput(unicode, TranslateModifiers());
}

static void CaptureMouse(HWND hwnd){
    if(GetCapture() != hwnd)
        SetCapture(hwnd);
}

static void ReleaseMouseIfNoButtonIsDown(HWND hwnd){
    if(GetCapture() != hwnd)
        return;

    if((GetKeyState(VK_LBUTTON) & 0x8000)
        || (GetKeyState(VK_RBUTTON) & 0x8000)
        || (GetKeyState(VK_MBUTTON) & 0x8000)
        || (GetKeyState(VK_XBUTTON1) & 0x8000)
        || (GetKeyState(VK_XBUTTON2) & 0x8000))
        return;

    ReleaseCapture();
}

static void DispatchMousePosition(Frame& frame, LPARAM lParam){
    frame.input().mousePosUpdate(static_cast<f64>(SignedLowWord(lParam)), static_cast<f64>(SignedHighWord(lParam)));
}

static void DispatchScreenMousePosition(HWND hwnd, Frame& frame, LPARAM lParam){
    POINT point = { SignedLowWord(lParam), SignedHighWord(lParam) };
    if(ScreenToClient(hwnd, &point))
        frame.input().mousePosUpdate(static_cast<f64>(point.x), static_cast<f64>(point.y));
}

static void DispatchKeyEvent(Frame& frame, WPARAM wParam, LPARAM lParam, i32 action){
    const i32 key = TranslateKey(wParam, lParam);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers());
    frame.input().keyboardUpdate(key, TranslateScancode(lParam), action, mods);
}

static void DispatchCharInput(Frame& frame, WPARAM wParam){
#if defined(NWB_UNICODE)
    static u16 highSurrogate = 0;
    const u32 codeUnit = static_cast<u32>(wParam);

    if(codeUnit >= 0xd800u && codeUnit <= 0xdbffu){
        highSurrogate = static_cast<u16>(codeUnit);
        return;
    }

    if(codeUnit >= 0xdc00u && codeUnit <= 0xdfffu){
        if(highSurrogate != 0){
            const u32 unicode = 0x10000u
                + ((static_cast<u32>(highSurrogate) - 0xd800u) << 10)
                + (codeUnit - 0xdc00u)
            ;
            highSurrogate = 0;
            DispatchUnicodeInput(frame, unicode);
        }
        return;
    }

    highSurrogate = 0;
    DispatchUnicodeInput(frame, codeUnit);
#else
    DispatchUnicodeInput(frame, static_cast<u32>(wParam & 0xffu));
#endif
}

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
                (void)hdc;
                ret = _this->render();
                EndPaint(hwnd, &ps);
            }
            if(!ret)
                PostQuitMessage(0);
        }
        return 0;

        case WM_KEYDOWN:
        {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << 30))
                ? InputAction::Repeat
                : InputAction::Press
            ;
            DispatchKeyEvent(*_this, wParam, lParam, action);
        }
        return 0;

        case WM_SYSKEYDOWN:
        {
            const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << 30))
                ? InputAction::Repeat
                : InputAction::Press
            ;
            DispatchKeyEvent(*_this, wParam, lParam, action);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_KEYUP:
        {
            DispatchKeyEvent(*_this, wParam, lParam, InputAction::Release);
        }
        return 0;

        case WM_SYSKEYUP:
        {
            DispatchKeyEvent(*_this, wParam, lParam, InputAction::Release);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_CHAR:
        {
            DispatchCharInput(*_this, wParam);
        }
        return 0;

        case WM_SYSCHAR:
        {
            DispatchCharInput(*_this, wParam);
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_UNICHAR:
        {
            if(wParam == UNICODE_NOCHAR)
                return TRUE;
            DispatchUnicodeInput(*_this, static_cast<u32>(wParam));
        }
        return 0;

        case WM_MOUSEMOVE:
        {
            DispatchMousePosition(*_this, lParam);
        }
        return 0;

        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            DispatchMousePosition(*_this, lParam);
            _this->input().mouseButtonUpdate(TranslateMouseButton(uMsg, wParam), InputAction::Press, TranslateModifiers());
            CaptureMouse(hwnd);
        }
        return (uMsg == WM_XBUTTONDOWN) ? TRUE : 0;

        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            DispatchMousePosition(*_this, lParam);
            _this->input().mouseButtonUpdate(TranslateMouseButton(uMsg, wParam), InputAction::Release, TranslateModifiers());
            ReleaseMouseIfNoButtonIsDown(hwnd);
        }
        return (uMsg == WM_XBUTTONUP) ? TRUE : 0;

        case WM_MOUSEWHEEL:
        {
            DispatchScreenMousePosition(hwnd, *_this, lParam);
            _this->input().mouseScrollUpdate(0.0, static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f64>(WHEEL_DELTA));
        }
        return 0;

        case WM_MOUSEHWHEEL:
        {
            DispatchScreenMousePosition(hwnd, *_this, lParam);
            _this->input().mouseScrollUpdate(static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f64>(WHEEL_DELTA), 0.0);
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

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Frame: Using Windows backend."));
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
            if(const tchar* title = syncGraphicsWindowState(width, height, windowVisible, windowIsInFocus)){
#ifdef NWB_UNICODE
                SetWindowTextW(data<Common::WinFrame>().hwnd(), title);
#else
                SetWindowTextA(data<Common::WinFrame>().hwnd(), title);
#endif
            }

            Timer currentTime(TimerNow());
            auto timeDifference = DurationInSeconds<f32>(currentTime, lateTime);
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
