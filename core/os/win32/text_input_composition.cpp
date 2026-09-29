// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input_context.h"

#include <core/common/log.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum Win32TextInputService::readCompositionText(const NotNull<void*> nativeContext, const u32 index){
    const HIMC context = static_cast<HIMC>(nativeContext.get());
    const LONG byteCount = ImmGetCompositionStringW(context, index, nullptr, 0u);
    if(byteCount < 0)
        return TextInputAdmission::NativeFailure;
    if((static_cast<usize>(byteCount) % sizeof(wchar)) != 0u)
        return TextInputAdmission::InvalidText;
    if(static_cast<usize>(byteCount) > s_TextInputMaxEventTextBytes * sizeof(wchar))
        return TextInputAdmission::TooLarge;
    m_wideText.resize(static_cast<usize>(byteCount) / sizeof(wchar));
    m_utf8Text.clear();
    if(byteCount == 0)
        return TextInputAdmission::Accepted;
    const LONG readBytes = ImmGetCompositionStringW(context, index, m_wideText.data(), static_cast<DWORD>(byteCount));
    if(readBytes != byteCount)
        return TextInputAdmission::NativeFailure;
    const i32 utf8Length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), static_cast<i32>(m_wideText.size()), nullptr, 0, nullptr, nullptr
    );
    if(utf8Length == 0)
        return TextInputAdmission::InvalidText;
    if(static_cast<usize>(utf8Length) > s_TextInputMaxEventTextBytes)
        return TextInputAdmission::TooLarge;
    m_utf8Text.resize(static_cast<usize>(utf8Length));
    const i32 converted = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), static_cast<i32>(m_wideText.size()),
        m_utf8Text.data(), utf8Length, nullptr, nullptr
    );
    return converted == utf8Length ? TextInputAdmission::Accepted : TextInputAdmission::InvalidText;
}

TextInputAdmission::Enum Win32TextInputService::compositionCursor(const NotNull<void*> nativeContext, usize& byteOffset){
    const LONG cursor = ImmGetCompositionStringW(static_cast<HIMC>(nativeContext.get()), GCS_CURSORPOS, nullptr, 0u);
    if(cursor < 0 || static_cast<usize>(cursor) > m_wideText.size())
        return TextInputAdmission::InvalidRange;
    byteOffset = 0u;
    if(cursor == 0)
        return TextInputAdmission::Accepted;
    const i32 prefixLength = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), cursor, nullptr, 0, nullptr, nullptr
    );
    if(prefixLength == 0)
        return TextInputAdmission::InvalidRange;
    byteOffset = static_cast<usize>(prefixLength);
    return TextInputAdmission::Accepted;
}

TextInputAdmission::Enum Win32TextInputService::acceptComposition(
    const TextInputSessionToken token,
    const usize wParam,
    const isize flags){
    if((flags & CS_INSERTCHAR) != 0 && (flags & (GCS_RESULTSTR | GCS_COMPSTR)) == 0){
        if(wParam > 0xffffu)
            return TextInputAdmission::InvalidText;
        return acceptInsertedPreedit(token, static_cast<u32>(wParam), (flags & CS_NOMOVECARET) == 0);
    }
    if(flags == 0){
        clearCompositionPreedit();
        return publishCompositionPreedit(token, {}, 0u, 0u);
    }
    const HWND window = static_cast<HWND>(m_nativeWindowHandle.get());
    const HIMC context = ImmGetContext(window);
    if(!context)
        return TextInputAdmission::Unavailable;
    Win32TextInputContextGuard release(window, context);
    if((flags & GCS_RESULTSTR) != 0){
        const TextInputAdmission::Enum result = readCompositionText(MakeNotNull(static_cast<void*>(context)), GCS_RESULTSTR);
        if(result != TextInputAdmission::Accepted)
            return result;
        const TextInputAdmission::Enum committed = emitCommit(token, m_utf8Text);
        if(committed != TextInputAdmission::Accepted)
            return committed;
        clearCompositionPreedit();
        const TextInputAdmission::Enum cleared = publishCompositionPreedit(token, {}, 0u, 0u);
        if(cleared != TextInputAdmission::Accepted)
            return cleared;
    }
    if((flags & (GCS_COMPSTR | GCS_CURSORPOS)) != 0){
        const TextInputAdmission::Enum result = readCompositionText(MakeNotNull(static_cast<void*>(context)), GCS_COMPSTR);
        if(result != TextInputAdmission::Accepted)
            return result;
        usize cursorByte = 0u;
        const TextInputAdmission::Enum cursor = compositionCursor(MakeNotNull(static_cast<void*>(context)), cursorByte);
        if(cursor != TextInputAdmission::Accepted)
            return cursor;
        return publishCompositionPreedit(token, m_utf8Text, cursorByte, cursorByte);
    }
    return TextInputAdmission::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

