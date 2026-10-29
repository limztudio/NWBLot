// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"

#include <core/os/text_input_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum Win32TextInputService::acceptInsertedPreedit(const TextInputSessionToken token, const u32 unit, const b8 moveCaret){
    if(IsUtf16HighSurrogate(unit)){
        if(m_pendingPreeditHighSurrogate != 0u)
            return TextInputAdmission::InvalidText;
        m_pendingPreeditHighSurrogate = unit;
        m_pendingPreeditMoveCaret = moveCaret;
        return TextInputAdmission::Accepted;
    }
    u32 codePoint = unit;
    b8 advanceCaret = moveCaret;
    if(IsUtf16LowSurrogate(unit)){
        if(m_pendingPreeditHighSurrogate == 0u)
            return TextInputAdmission::InvalidText;
        codePoint = DecodeUtf16SurrogatePair(m_pendingPreeditHighSurrogate, unit);
        advanceCaret = advanceCaret && m_pendingPreeditMoveCaret;
        m_pendingPreeditHighSurrogate = 0u;
    }
    else if(m_pendingPreeditHighSurrogate != 0u){
        m_pendingPreeditHighSurrogate = 0u;
        return TextInputAdmission::InvalidText;
    }
    char bytes[s_Utf8MaxSequenceBytes];
    const auto encoded = EncodeTextInputCodePoint(codePoint, bytes);
    if(!encoded)
        return encoded.error();
    const usize length = *encoded;
    if(length > s_TextInputMaxEventTextBytes - m_preeditText.size())
        return TextInputAdmission::TooLarge;
    m_utf8Text.assign(m_preeditText.data(), m_preeditText.size());
    m_utf8Text.insert(m_preeditCaretByte, bytes, length);
    const usize caret = m_preeditCaretByte + (advanceCaret ? length : 0u);
    return publishCompositionPreedit(token, m_utf8Text, caret, caret);
}

TextInputAdmission::Enum Win32TextInputService::publishCompositionPreedit(const TextInputSessionToken token, const AStringView text, const usize anchorByte, const usize caretByte){
    const TextInputAdmission::Enum admitted = emitPreedit(token, text, anchorByte, caretByte);
    if(admitted == TextInputAdmission::Accepted){
        m_preeditText.assign(text.data(), text.size());
        m_preeditCaretByte = caretByte;
    }
    return admitted;
}

void Win32TextInputService::clearCompositionPreedit()noexcept{
    m_preeditText.clear();
    m_preeditCaretByte = 0u;
    m_pendingPreeditHighSurrogate = 0u;
    m_pendingPreeditMoveCaret = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

