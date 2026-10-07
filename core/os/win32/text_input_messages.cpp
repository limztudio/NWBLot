// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "input_message.h"

#include <core/os/text_input_text.h>
#include <core/common/log.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Win32TextInputService::handleMessage(const u32 message, const usize wParam, const isize lParam){
    if(!isOwnerThread())
        return false;
    if(message == WM_SETFOCUS || message == WM_KILLFOCUS){
        if(!resetFallbackCharInput() || !setFocused(message == WM_SETFOCUS))
            NWB_LOGGER_WARNING(NWB_TEXT("Text input: native keyboard-focus update failed"));
        return true;
    }
    const TextInputSessionToken token = activeSession();
    if(!token.valid())
        return false;
    TextInputAdmission::Enum admission = TextInputAdmission::Accepted;
    const u32 repeatCount = Win32MessageRepeatCount(lParam);
    switch(message){
    case WM_CHAR:
    case WM_SYSCHAR:
        admission = wParam <= 0xffffu
            ? acceptUtf16Unit(token, static_cast<u32>(wParam), repeatCount) : TextInputAdmission::InvalidText
        ;
        break;
    case WM_UNICHAR:
        if(wParam == UNICODE_NOCHAR)
            return false;
        m_pendingHighSurrogate = 0u;
        admission = wParam <= 0x10ffffu
            ? acceptCodePoint(token, static_cast<u32>(wParam), repeatCount) : TextInputAdmission::InvalidText
        ;
        break;
    case WM_IME_STARTCOMPOSITION:
        m_pendingHighSurrogate = 0u;
        m_compositionToken = token;
        clearCompositionPreedit();
        admission = publishCompositionPreedit(token, {}, 0u, 0u);
        break;
    case WM_IME_COMPOSITION:
        if(m_compositionToken != token)
            return true;
        admission = acceptComposition(token, wParam, lParam);
        break;
    case WM_IME_ENDCOMPOSITION:
        m_compositionToken = {};
        clearCompositionPreedit();
        admission = publishCompositionPreedit(token, {}, 0u, 0u);
        break;
    case WM_IME_CHAR:
        // GCS_RESULTSTR already supplied the commit. DefWindowProc would turn this into duplicate WM_CHAR messages.
        return true;
    default:
        return false;
    }
    if(admission != TextInputAdmission::Accepted)
        rejectNativeInput(token, admission);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum Win32TextInputService::acceptCodePoint(
    const TextInputSessionToken token,
    const u32 codePoint,
    const u32 repeatCount
){
    // Editing/navigation control keys are delivered through the ordinary key path, never committed as document text.
    if(codePoint < 0x20u || codePoint == 0x7fu)
        return TextInputAdmission::Accepted;
    char bytes[4] = {};
    usize length = 0u;
    const TextInputAdmission::Enum encoded = EncodeTextInputCodePoint(codePoint, bytes, length);
    if(encoded != TextInputAdmission::Accepted)
        return encoded;
    if(repeatCount == 1u)
        return emitCommit(token, AStringView(bytes, length));
    const usize batchCount = Min<usize>(repeatCount, s_TextInputMaxEventTextBytes / length);
    m_utf8Text.clear();
    m_utf8Text.reserve(batchCount * length);
    for(usize index = 0u; index < batchCount; ++index)
        m_utf8Text.append(bytes, length);
    usize remaining = repeatCount;
    while(remaining != 0u){
        const usize count = Min(remaining, batchCount);
        const TextInputAdmission::Enum status = emitCommit(token, AStringView(m_utf8Text.data(), count * length));
        if(status != TextInputAdmission::Accepted)
            return status;
        remaining -= count;
    }
    return TextInputAdmission::Accepted;
}

TextInputAdmission::Enum Win32TextInputService::acceptUtf16Unit(
    const TextInputSessionToken token,
    const u32 unit,
    const u32 repeatCount
){
    if(unit > 0xffffu){
        m_pendingHighSurrogate = 0u;
        return TextInputAdmission::InvalidText;
    }
    if(unit >= 0xd800u && unit <= 0xdbffu){
        const bool interruptedPair = m_pendingHighSurrogate != 0u;
        m_pendingHighSurrogate = unit;
        return interruptedPair ? TextInputAdmission::InvalidText : TextInputAdmission::Accepted;
    }
    if(unit >= 0xdc00u && unit <= 0xdfffu){
        if(m_pendingHighSurrogate == 0u)
            return TextInputAdmission::InvalidText;
        const u32 codePoint = 0x10000u + ((m_pendingHighSurrogate - 0xd800u) << 10u) + (unit - 0xdc00u);
        m_pendingHighSurrogate = 0u;
        return acceptCodePoint(token, codePoint, repeatCount);
    }
    if(m_pendingHighSurrogate != 0u){
        m_pendingHighSurrogate = 0u;
        return TextInputAdmission::InvalidText;
    }
    return acceptCodePoint(token, unit, repeatCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Win32TextInputService::rejectNativeInput(const TextInputSessionToken token, const TextInputAdmission::Enum admission){
    NWB_LOGGER_WARNING(NWB_TEXT("Text input: native delivery rejected ({})"), static_cast<u32>(admission));
    if(admission != TextInputAdmission::QueueFull && token == activeSession()){
        if(!cancelSession(token, TextInputCancelReason::NativeFailure))
            NWB_FATAL_ASSERT(false);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

