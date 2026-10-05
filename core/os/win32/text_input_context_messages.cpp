// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input.h"

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Win32TextInputService::resolveContextMessage(
    const u32 message, const usize wParam, const isize lParam, isize& forwardedLParam){
    if(!isOwnerThread() || message != WM_IME_SETCONTEXT)
        return false;
    // Keep the native flags, not the filtered result: ending a custom edit restores the ordinary IME UI.
    m_nativeContextFlags = lParam;
    m_nativeContextKnown = true;
    m_nativeContextActive = wParam != FALSE;
    forwardedLParam = lParam;
    if(m_nativeContextActive && focused() && activeSession().valid())
        forwardedLParam &= ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW);
    return true;
}

void Win32TextInputService::replayContextVisibility(){
    if(m_replayingContext || !isOwnerThread() || !m_nativeContextKnown || !m_nativeContextActive || !focused())
        return;
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(!IsWindow(window))
        return;
    // Activation may precede the edit session. Reapply the last native options when ownership changes.
    m_replayingContext = true;
    const bool ownedBefore = activeSession().valid();
    SendMessageW(window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(m_nativeContextFlags));
    // Native forwarding may synchronously cancel the session. Restore the now-current policy once in that case.
    if(ownedBefore != activeSession().valid() && m_nativeContextActive && focused() && IsWindow(window))
        SendMessageW(window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(m_nativeContextFlags));
    m_replayingContext = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveWin32TextInputContextMessage(
    ITextInputService& service,
    const u32 message,
    const usize wParam,
    const isize lParam,
    isize& forwardedLParam){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return CheckedCast<Win32TextInputService*>(&service)->resolveContextMessage(
        message, wParam, lParam, forwardedLParam
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

