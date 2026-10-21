// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "win32_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "input_helpers.h"

#include <core/common/log.h>
#include <core/os/win32/text_input.h>
#include <core/os/win32/input_message.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_win32_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_Win32ExtendedKeyBit = 24u;
static constexpr usize s_Win32ScancodeShift = 16u;
static constexpr usize s_Win32PreviousKeyStateBit = 30u;
static constexpr usize s_Win32ScancodeMask = 0x1ffu;
static constexpr usize s_Win32VirtualScancodeMask = 0xffu;
static constexpr usize s_Win32WordMask = 0xffffu;
static constexpr int s_Win32KeyDownMask = 0x8000;
static constexpr int s_Win32KeyToggleMask = 0x0001;
static constexpr u32 s_AsciiDigitFirst = '0';
static constexpr u32 s_AsciiDigitLast = '9';
static constexpr u32 s_AsciiUpperFirst = 'A';
static constexpr u32 s_AsciiUpperLast = 'Z';


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool IsExtendedKey(LPARAM lParam)noexcept{
    return (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32ExtendedKeyBit)) != 0;
}

static i32 TranslateScancode(LPARAM lParam)noexcept{
    return static_cast<i32>((static_cast<usize>(lParam) >> s_Win32ScancodeShift) & s_Win32ScancodeMask);
}

static i32 TranslateModifiers()noexcept{
    i32 mods = 0;

    if(GetKeyState(VK_SHIFT) & s_Win32KeyDownMask)
        mods |= InputModifier::Shift;
    if(GetKeyState(VK_CONTROL) & s_Win32KeyDownMask)
        mods |= InputModifier::Control;
    if(GetKeyState(VK_MENU) & s_Win32KeyDownMask)
        mods |= InputModifier::Alt;
    if((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & s_Win32KeyDownMask)
        mods |= InputModifier::Super;
    if(GetKeyState(VK_CAPITAL) & s_Win32KeyToggleMask)
        mods |= InputModifier::CapsLock;
    if(GetKeyState(VK_NUMLOCK) & s_Win32KeyToggleMask)
        mods |= InputModifier::NumLock;

    return mods;
}

static i32 TranslateShiftKey(LPARAM lParam)noexcept{
    const UINT scancode = static_cast<UINT>((static_cast<usize>(lParam) >> s_Win32ScancodeShift) & s_Win32VirtualScancodeMask);
    switch(MapVirtualKeyW(scancode, MAPVK_VSC_TO_VK_EX)){
    case VK_RSHIFT:
        return Key::RightShift;
    case VK_LSHIFT:
    default:
        return Key::LeftShift;
    }
}

static i32 TranslateNavigationKey(i32 extendedKey, i32 keypadKey, LPARAM lParam)noexcept{
    return IsExtendedKey(lParam) ? extendedKey : keypadKey;
}

static i32 TranslateKey(WPARAM wParam, LPARAM lParam)noexcept{
    const u32 vk = static_cast<u32>(wParam);

    if(vk >= s_AsciiDigitFirst && vk <= s_AsciiDigitLast)
        return static_cast<i32>(Key::Number0 + (vk - s_AsciiDigitFirst));

    if(vk >= s_AsciiUpperFirst && vk <= s_AsciiUpperLast)
        return static_cast<i32>(Key::A + (vk - s_AsciiUpperFirst));

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

static i32 SignedLowWord(LPARAM value)noexcept{
    return static_cast<i32>(static_cast<i16>(static_cast<u16>(static_cast<usize>(value) & s_Win32WordMask)));
}

static i32 SignedHighWord(LPARAM value)noexcept{
    return static_cast<i32>(static_cast<i16>(static_cast<u16>((static_cast<usize>(value) >> s_Win32ScancodeShift) & s_Win32WordMask)));
}

static i32 TranslateMouseButton(UINT message, WPARAM wParam)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Optional<LRESULT> Win32FrameInput::handleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam){
    using namespace __hidden_frame_win32_input;

    const bool ordinaryText = message == WM_CHAR || message == WM_SYSCHAR || (message == WM_UNICHAR && wParam != UNICODE_NOCHAR);
    const i32 textScancode = ordinaryText ? TranslateScancode(lParam) : 0;
    // Text without a producing scan code is explicit native text, independent of a physical shortcut.
    if(ordinaryText && textScancode != 0 && m_frame.input().keyboardTextBlocked(textScancode)){
        if(auto* textInput = m_frame.tryTextInput()){
            if(!ResetWin32FallbackCharInput(*textInput))
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 suppressed text-input character reset failed"));
        }
        return LRESULT{ 0 };
    }

    if(auto* textInput = m_frame.tryTextInput()){
        const auto forwardedLParam = ResolveWin32TextInputContextMessage(*textInput, message, wParam, lParam);
        if(forwardedLParam)
            return DefWindowProc(m_hwnd, message, wParam, static_cast<LPARAM>(*forwardedLParam));
        if(DispatchWin32TextInputMessage(*textInput, message, wParam, lParam))
            return LRESULT{ 0 };
    }

    switch(message){
    case WM_KEYDOWN: {
        const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32PreviousKeyStateBit)) ? InputAction::Repeat : InputAction::Press;
        dispatchKeyEvent(wParam, lParam, action);
    }
    return LRESULT{ 0 };

    case WM_SYSKEYDOWN: {
        const i32 action = (static_cast<usize>(lParam) & (static_cast<usize>(1) << s_Win32PreviousKeyStateBit)) ? InputAction::Repeat : InputAction::Press;
        dispatchKeyEvent(wParam, lParam, action);
    }
    return DefWindowProc(m_hwnd, message, wParam, lParam);

    case WM_KEYUP: {
        dispatchKeyEvent(wParam, lParam, InputAction::Release);
    }
    return LRESULT{ 0 };

    case WM_SYSKEYUP: {
        dispatchKeyEvent(wParam, lParam, InputAction::Release);
    }
    return DefWindowProc(m_hwnd, message, wParam, lParam);

    case WM_CHAR: {
        dispatchCharInput(wParam, lParam);
    }
    return LRESULT{ 0 };

    case WM_SYSCHAR: {
        dispatchCharInput(wParam, lParam);
    }
    return DefWindowProc(m_hwnd, message, wParam, lParam);

    case WM_UNICHAR: {
        if(wParam == UNICODE_NOCHAR)
            return LRESULT{ TRUE };
        if(GetFocus() != m_hwnd || !m_frame.data<Common::WinFrame>().isActive())
            return LRESULT{ 0 };
        if(auto* textInput = m_frame.tryTextInput()){
            if(!ResetWin32FallbackCharInput(*textInput))
                NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input character reset failed"));
        }
        for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
            dispatchUnicodeInput(static_cast<u32>(wParam));
    }
    return LRESULT{ 0 };

    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hwnd, 0u };
        if(!TrackMouseEvent(&tracking)){
            const DWORD nativeError = GetLastError();
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 mouse-leave tracking failed; native_domain=Win32 native_code={}"), nativeError);
        }
        dispatchMousePosition(lParam);
    }
    return LRESULT{ 0 };

    case WM_MOUSELEAVE:
        m_frame.input().pointerLeave();
        return LRESULT{ 0 };

    case WM_CAPTURECHANGED:
        if(reinterpret_cast<HWND>(lParam) != m_hwnd)
            m_frame.input().pointerCaptureLost();
        return LRESULT{ 0 };

    case WM_CANCELMODE:
        m_frame.input().pointerCaptureLost();
        if(GetCapture() == m_hwnd && !ReleaseCapture()){
            const DWORD nativeError = GetLastError();
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 pointer capture release failed; native_domain=Win32 native_code={}"), nativeError);
        }
        return LRESULT{ 0 };

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    {
        dispatchMousePosition(lParam);
        m_frame.input().mouseButtonUpdate(TranslateMouseButton(message, wParam), InputAction::Press, TranslateModifiers());
        captureMouse();
    }
    return LRESULT{ (message == WM_XBUTTONDOWN) ? TRUE : 0 };

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
    {
        dispatchMousePosition(lParam);
        m_frame.input().mouseButtonUpdate(TranslateMouseButton(message, wParam), InputAction::Release, TranslateModifiers());
        releaseMouseIfNoButtonIsDown();
    }
    return LRESULT{ (message == WM_XBUTTONUP) ? TRUE : 0 };

    case WM_MOUSEWHEEL: {
        dispatchScreenMousePosition(lParam);
        m_frame.input().mouseScrollUpdate(0.0, VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam))), VectorDoubleReplicate(static_cast<f64>(WHEEL_DELTA)))));
    }
    return LRESULT{ 0 };

    case WM_MOUSEHWHEEL: {
        dispatchScreenMousePosition(lParam);
        m_frame.input().mouseScrollUpdate(VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(static_cast<f64>(GET_WHEEL_DELTA_WPARAM(wParam))), VectorDoubleReplicate(static_cast<f64>(WHEEL_DELTA)))), 0.0);
    }
    return LRESULT{ 0 };

    default:
        return {};
    }
}

void Win32FrameInput::dispatchTextInputFocus(const bool focused){
    if(auto* textInput = m_frame.tryTextInput()){
        if(!ResetWin32FallbackCharInput(*textInput))
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input character reset failed"));
        if(!textInput->setFocused(focused))
            NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 text-input focus update failed"));
    }
}


void Win32FrameInput::dispatchUnicodeInput(const u32 unicode){
    if(IsTextInputCodePoint(unicode))
        m_frame.input().keyboardCharInput(unicode, __hidden_frame_win32_input::TranslateModifiers());
}

void Win32FrameInput::captureMouse(){
    if(GetCapture() != m_hwnd)
        SetCapture(m_hwnd);
}

void Win32FrameInput::releaseMouseIfNoButtonIsDown(){
    using namespace __hidden_frame_win32_input;

    if(GetCapture() != m_hwnd)
        return;

    if((GetKeyState(VK_LBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_RBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_MBUTTON) & s_Win32KeyDownMask) || (GetKeyState(VK_XBUTTON1) & s_Win32KeyDownMask) || (GetKeyState(VK_XBUTTON2) & s_Win32KeyDownMask))
        return;

    if(!ReleaseCapture()){
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_ERROR(NWB_TEXT("Frame Win32 pointer capture release failed; native_domain=Win32 native_code={}"), nativeError);
    }
}

void Win32FrameInput::dispatchMousePosition(const LPARAM lParam){
    using namespace __hidden_frame_win32_input;

    m_frame.input().mousePosUpdate(static_cast<f64>(SignedLowWord(lParam)), static_cast<f64>(SignedHighWord(lParam)));
}

void Win32FrameInput::dispatchScreenMousePosition(const LPARAM lParam){
    using namespace __hidden_frame_win32_input;

    POINT point = { SignedLowWord(lParam), SignedHighWord(lParam) };
    if(ScreenToClient(m_hwnd, &point))
        m_frame.input().mousePosUpdate(static_cast<f64>(point.x), static_cast<f64>(point.y));
    else{
        const DWORD nativeError = GetLastError();
        NWB_LOGGER_WARNING(NWB_TEXT("Frame Win32 mouse position conversion failed; native_domain=Win32 native_code={}"), nativeError);
    }
}

void Win32FrameInput::dispatchKeyEvent(const WPARAM wParam, const LPARAM lParam, const i32 action){
    using namespace __hidden_frame_win32_input;

    const i32 key = TranslateKey(wParam, lParam);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers());
    const u32 count = action == InputAction::Release ? 1u : Win32MessageRepeatCount(lParam);
    for(u32 index = 0u; index < count; ++index)
        m_frame.input().keyboardUpdate(key, TranslateScancode(lParam), index == 0u ? action : InputAction::Repeat, mods);
}

void Win32FrameInput::dispatchCharInput(const WPARAM wParam, const LPARAM lParam){
#if defined(NWB_UNICODE)
    if(auto* textInput = m_frame.tryTextInput()){
        const auto unicode = DecodeWin32FallbackCharInput(*textInput, static_cast<u32>(wParam));
        if(unicode){
            for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
                dispatchUnicodeInput(*unicode);
        }
    }
#else
    for(u32 index = 0u; index < Win32MessageRepeatCount(lParam); ++index)
        dispatchUnicodeInput(static_cast<u32>(wParam & s_AnsiCharMask));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

