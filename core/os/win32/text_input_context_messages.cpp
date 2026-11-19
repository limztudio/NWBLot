// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input.h"

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<isize> Win32TextInputService::resolveContextMessage(const u32 message, const usize wParam, const isize lParam)noexcept{
    if(!isOwnerThread() || message != WM_IME_SETCONTEXT)
        return MakeUnexpected(Failure{});
    m_nativeContextFlags = lParam;
    m_nativeContextKnown = true;
    m_nativeContextActive = wParam != FALSE;
    isize forwardedLParam = lParam;
    if(m_nativeContextActive && focused() && activeSession().valid())
        forwardedLParam &= ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW);
    return forwardedLParam;
}

void Win32TextInputService::replayContextVisibility(){
    if(m_replayingContext || !isOwnerThread() || !m_nativeContextKnown || !m_nativeContextActive || !focused())
        return;
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(!IsWindow(window))
        return;
    m_replayingContext = true;
    const b8 ownedBefore = activeSession().valid();
    SendMessageW(window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(m_nativeContextFlags));
    if(ownedBefore != activeSession().valid() && m_nativeContextActive && focused() && IsWindow(window))
        SendMessageW(window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(m_nativeContextFlags));
    m_replayingContext = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<isize> ResolveWin32TextInputContextMessage(ITextInputService& service, const u32 message, const usize wParam, const isize lParam)noexcept{
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return MakeUnexpected(Failure{});
    return MakeNotNull(checked_cast<Win32TextInputService*>(&service))->resolveContextMessage(message, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

