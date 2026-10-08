// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Forward active-session WM_CHAR/WM_SYSCHAR/WM_UNICHAR and IMM32 composition messages before ordinary input.
// true consumes the message: Frame must not call DefWindowProc, which otherwise synthesizes duplicate result characters.
// Foreign backends and inactive text messages return false; keyboard-focus messages still reset native session eligibility. Frame handles the WM_UNICHAR UNICODE_NOCHAR capability probe independently.
[[nodiscard]] bool DispatchWin32TextInputMessage(ITextInputService& service, u32 message, usize wParam, isize lParam);

// Returns forwarding flags for an owned WM_IME_SETCONTEXT. Forward those flags to DefWindowProc and return
// its result; this bridge never consumes the native context message.
[[nodiscard]] Expected<isize> ResolveWin32TextInputContextMessage(
    ITextInputService& service, u32 message, usize wParam, isize lParam
);

// Scene character decoding borrows the same per-service UTF-16 state. Session begin/end clear it before ownership changes.
// Returns a complete non-NUL scalar from a focused, inactive Win32 service.
[[nodiscard]] Expected<u32> DecodeWin32FallbackCharInput(ITextInputService& service, u32 unit);
// Frame calls this on every native focus transition, including transitions with no active editing session.
[[nodiscard]] bool ResetWin32FallbackCharInput(ITextInputService& service);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

