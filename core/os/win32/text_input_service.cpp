// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input_context.h"
#include "text_input.h"

#include <core/common/log.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Win32TextInputService::Win32TextInputService(Alloc::GlobalArena& arena, const NotNull<void*> nativeWindowHandle)
    : QueuedTextInputService(arena)
    , m_nativeWindowHandle(nativeWindowHandle)
    , m_wideText(arena)
    , m_utf8Text(arena)
    , m_preeditText(arena)
{
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(IsWindow(window)){
        const HIMC context = ImmGetContext(window);
        if(context){
            Win32TextInputContextGuard release(window, context);
            m_imeAvailable = true;
        }
    }
}

Win32TextInputService::~Win32TextInputService(){
    const TextInputSessionToken token = activeSession();
    if(token.valid() && !end(token))
        NWB_FATAL_ASSERT(false);
}

TextInputCapabilities Win32TextInputService::capabilities()const noexcept{
    return {
        .commit = true, .preedit = m_imeAvailable, .surrounding = false, .deleteSurrounding = false,
        .backend = TextInputBackend::Win32Imm32
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum Win32TextInputService::startNativeSession(
    const TextInputSessionToken token,
    const TextInputSessionDesc& desc
){
    static_cast<void>(token);
    if(!IsWindow(static_cast<HWND>(m_nativeWindowHandle.get())))
        return TextInputAdmission::Unavailable;
    m_pendingHighSurrogate = 0u;
    m_compositionToken = {};
    clearCompositionPreedit();
    const TextInputAdmission::Enum admission = updateNativeCaret(desc.caret);
    if(admission == TextInputAdmission::Accepted)
        replayContextVisibility();
    return admission;
}

void Win32TextInputService::endNativeSession(const TextInputSessionToken token){
    const bool composing = m_compositionToken == token;
    m_pendingHighSurrogate = 0u;
    m_compositionToken = {};
    clearCompositionPreedit();
    m_wideText.clear();
    m_utf8Text.clear();
    if(composing){
        const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
        if(IsWindow(window)){
            const HIMC context = ImmGetContext(window);
            if(context){
                Win32TextInputContextGuard release(window, context);
                if(!ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0u))
                    NWB_LOGGER_WARNING(NWB_TEXT("Text input: IMM32 composition cancellation unavailable"));
            }
        }
    }
    replayContextVisibility();
}

TextInputAdmission::Enum Win32TextInputService::updateNativeCaret(const TextInputRect caret){
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    if(!IsWindow(window))
        return TextInputAdmission::Unavailable;
    const HIMC context = ImmGetContext(window);
    m_imeAvailable = context != nullptr;
    if(!context)
        return TextInputAdmission::Accepted;
    Win32TextInputContextGuard release(window, context);
    COMPOSITIONFORM composition = {};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = { caret.x, caret.y };
    CANDIDATEFORM candidate = {};
    candidate.dwIndex = 0u;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = { caret.x, caret.y + caret.height };
    candidate.rcArea = { caret.x, caret.y, caret.x + caret.width, caret.y + caret.height };
    const bool compositionSet = ImmSetCompositionWindow(context, &composition) != FALSE;
    const bool candidateSet = ImmSetCandidateWindow(context, &candidate) != FALSE;
    if(!compositionSet || !candidateSet){
        NWB_LOGGER_WARNING(NWB_TEXT("Text input: IMM32 caret placement failed"));
        return TextInputAdmission::NativeFailure;
    }
    return TextInputAdmission::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool DispatchWin32TextInputMessage(
    ITextInputService& service,
    const u32 message,
    const usize wParam,
    const isize lParam
){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Win32Imm32)
        return false;
    return checked_cast<Win32TextInputService*>(&service)->handleMessage(message, wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

