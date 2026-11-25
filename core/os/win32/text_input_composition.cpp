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
    const i32 utf8Length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), static_cast<i32>(m_wideText.size()), nullptr, 0, nullptr, nullptr);
    if(utf8Length == 0)
        return TextInputAdmission::InvalidText;
    if(static_cast<usize>(utf8Length) > s_TextInputMaxEventTextBytes)
        return TextInputAdmission::TooLarge;
    m_utf8Text.resize(static_cast<usize>(utf8Length));
    const i32 converted = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), static_cast<i32>(m_wideText.size()), m_utf8Text.data(), utf8Length, nullptr, nullptr);
    return converted == utf8Length ? TextInputAdmission::Accepted : TextInputAdmission::InvalidText;
}

Expected<usize, TextInputAdmission::Enum> Win32TextInputService::compositionCursor(const NotNull<void*> nativeContext)noexcept{
    const LONG cursor = ImmGetCompositionStringW(static_cast<HIMC>(nativeContext.get()), GCS_CURSORPOS, nullptr, 0u);
    if(cursor < 0 || static_cast<usize>(cursor) > m_wideText.size())
        return MakeUnexpected(TextInputAdmission::InvalidRange);
    if(cursor == 0)
        return 0u;
    const i32 prefixLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, m_wideText.data(), cursor, nullptr, 0, nullptr, nullptr);
    if(prefixLength == 0)
        return MakeUnexpected(TextInputAdmission::InvalidRange);
    return static_cast<usize>(prefixLength);
}

void Win32TextInputService::readCompositionClauses(const NotNull<void*> nativeContext, const AStringView utf8Text){
    m_preeditClauses.clear();
    if(utf8Text.empty() || m_wideText.empty())
        return;

    const HIMC context = static_cast<HIMC>(nativeContext.get());
    const LONG attrBytes = ImmGetCompositionStringW(context, GCS_COMPATTR, nullptr, 0u);
    if(attrBytes <= 0)
        return;
    const usize attrCount = Min(static_cast<usize>(attrBytes), m_wideText.size());
    m_compAttr.resize(static_cast<usize>(attrBytes));
    const LONG readAttr = ImmGetCompositionStringW(context, GCS_COMPATTR, m_compAttr.data(), static_cast<DWORD>(attrBytes));
    if(readAttr != attrBytes)
        return;

    m_utf16ByteOffsets.assign(m_wideText.size() + 1u, utf8Text.size());
    usize utf16Index = 0u;
    usize byteOffset = 0u;
    while(byteOffset < utf8Text.size() && utf16Index < m_wideText.size()){
        m_utf16ByteOffsets[utf16Index] = byteOffset;
        const u8 lead = static_cast<u8>(utf8Text[byteOffset]);
        usize sequenceBytes = 1u;
        usize utf16Units = 1u;
        if((lead & 0x80u) == 0u)
            sequenceBytes = 1u;
        else if((lead & 0xe0u) == 0xc0u)
            sequenceBytes = 2u;
        else if((lead & 0xf0u) == 0xe0u)
            sequenceBytes = 3u;
        else{
            sequenceBytes = 4u;
            utf16Units = 2u;
        }
        if(utf16Units == 2u && utf16Index + 1u < m_wideText.size())
            m_utf16ByteOffsets[utf16Index + 1u] = byteOffset;
        utf16Index += utf16Units;
        byteOffset += sequenceBytes;
    }

    b8 hasValidClauses = false;
    m_compClause.clear();
    const LONG clauseBytes = ImmGetCompositionStringW(context, GCS_COMPCLAUSE, nullptr, 0u);
    if(clauseBytes >= static_cast<LONG>(2u * sizeof(u32)) && (static_cast<usize>(clauseBytes) % sizeof(u32)) == 0u){
        m_compClause.resize(static_cast<usize>(clauseBytes) / sizeof(u32));
        const LONG readClause = ImmGetCompositionStringW(context, GCS_COMPCLAUSE, m_compClause.data(), static_cast<DWORD>(clauseBytes));
        if(readClause == clauseBytes && m_compClause[0u] == 0u && m_compClause.back() <= m_wideText.size()){
            hasValidClauses = true;
            for(usize index = 1u; index < m_compClause.size(); ++index){
                if(m_compClause[index] < m_compClause[index - 1u]){
                    hasValidClauses = false;
                    break;
                }
            }
        }
    }

    const auto mapAttrStyle = [](const u8 attr)noexcept -> TextInputPreeditClauseStyle::Enum{
        switch(attr){
        case ATTR_TARGET_CONVERTED:
        case ATTR_TARGET_NOTCONVERTED:
            return TextInputPreeditClauseStyle::Target;
        case ATTR_CONVERTED:
        case ATTR_FIXEDCONVERTED:
            return TextInputPreeditClauseStyle::Converted;
        default:
            return TextInputPreeditClauseStyle::Raw;
        }
    };

    const auto emitSpan = [&](const usize u16Begin, const usize u16End){
        usize runStart = u16Begin;
        while(runStart < u16End){
            const TextInputPreeditClauseStyle::Enum runStyle = mapAttrStyle(m_compAttr[runStart]);
            usize runEnd = runStart + 1u;
            while(runEnd < u16End && mapAttrStyle(m_compAttr[runEnd]) == runStyle)
                ++runEnd;
            const usize beginByte = m_utf16ByteOffsets[runStart];
            const usize endByte = m_utf16ByteOffsets[runEnd];
            if(beginByte < endByte && (m_preeditClauses.empty() || beginByte >= m_preeditClauses.back().endByte)){
                TextInputPreeditClause clause;
                clause.beginByte = beginByte;
                clause.endByte = endByte;
                clause.style = runStyle;
                m_preeditClauses.push_back(clause);
            }
            runStart = runEnd;
        }
    };

    if(hasValidClauses){
        for(usize index = 0u; index + 1u < m_compClause.size(); ++index){
            const usize u16Begin = Min(static_cast<usize>(m_compClause[index]), attrCount);
            const usize u16End = Min(static_cast<usize>(m_compClause[index + 1u]), attrCount);
            if(u16Begin < u16End)
                emitSpan(u16Begin, u16End);
        }
    }
    else
        emitSpan(0u, attrCount);
}

TextInputAdmission::Enum Win32TextInputService::acceptComposition(const TextInputSessionToken token, const usize wParam, const isize flags){
    if((flags & CS_INSERTCHAR) != 0 && (flags & (GCS_RESULTSTR | GCS_COMPSTR)) == 0){
        if(wParam > s_MaxU16)
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
    const TextInputAdmission::Enum admission = [&](){
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
        if((flags & (GCS_COMPSTR | GCS_CURSORPOS | GCS_COMPATTR | GCS_COMPCLAUSE)) != 0){
            const TextInputAdmission::Enum result = readCompositionText(MakeNotNull(static_cast<void*>(context)), GCS_COMPSTR);
            if(result != TextInputAdmission::Accepted)
                return result;
            const auto cursorByte = compositionCursor(MakeNotNull(static_cast<void*>(context)));
            if(!cursorByte)
                return cursorByte.error();
            readCompositionClauses(MakeNotNull(static_cast<void*>(context)), m_utf8Text);
            return publishCompositionPreedit(token, m_utf8Text, *cursorByte, *cursorByte, m_preeditClauses);
        }
        return TextInputAdmission::Accepted;
    }();
    if(!release.release())
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Text input: ImmReleaseContext failed"));
    return admission;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
