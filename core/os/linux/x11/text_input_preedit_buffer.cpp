// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_preedit.h"

#include <global/text_utils.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_text_input_preedit_buffer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidUtf8(const AStringView text)noexcept{
    return text.size() <= X11PreeditBuffer::s_MaxBytes && IsValidUtf8TextWithoutNul(text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11PreeditBuffer::X11PreeditBuffer(Alloc::GlobalArena& arena)noexcept
    : m_text(arena)
    , m_candidate(arena)
    , m_scalarStyles(arena)
    , m_scalarStylesCandidate(arena)
    , m_clauses(arena)
{}

void X11PreeditBuffer::clear()noexcept{
    m_text.clear();
    m_candidate.clear();
    m_scalarStyles.clear();
    m_scalarStylesCandidate.clear();
    m_clauses.clear();
    m_caretByte = 0u;
    m_hasFeedback = false;
}

b8 X11PreeditBuffer::replace(const usize firstCharacter, const usize characterCount, const AStringView insertion, const usize caretCharacter, const Span<const TextInputPreeditClauseStyle::Enum> insertionStyles){
    using namespace __hidden_x11_text_input_preedit_buffer;
    if(characterCount > s_MaxBytes || firstCharacter > s_MaxBytes - characterCount)
        return false;
    if(!ValidUtf8(insertion))
        return false;
    const auto firstByte = Utf8ByteOffset(m_text, firstCharacter);
    if(!firstByte)
        return false;
    const auto removedBytes = Utf8ByteOffset(AStringView(m_text).substr(*firstByte), characterCount);
    if(!removedBytes)
        return false;
    const usize lastByte = *firstByte + *removedBytes;
    const usize remainingBytes = m_text.size() - *removedBytes;
    if(insertion.size() > s_MaxBytes - remainingBytes)
        return false;
    const usize insertionScalars = CountUtf8ScalarsAssumingValid(insertion);
    if(!insertionStyles.empty() && insertionStyles.size() != insertionScalars)
        return false;
    const usize prefixEnd = Min(firstCharacter, m_scalarStyles.size());
    const usize suffixStart = Min(firstCharacter + characterCount, m_scalarStyles.size());
    const usize suffixCount = m_scalarStyles.size() - suffixStart;
    m_scalarStylesCandidate.clear();
    m_scalarStylesCandidate.reserve(prefixEnd + insertionScalars + suffixCount);
    for(usize index = 0u; index < prefixEnd; ++index)
        m_scalarStylesCandidate.push_back(m_scalarStyles[index]);
    if(!insertionStyles.empty()){
        for(const TextInputPreeditClauseStyle::Enum style : insertionStyles)
            m_scalarStylesCandidate.push_back(style);
    }
    else{
        for(usize index = 0u; index < insertionScalars; ++index)
            m_scalarStylesCandidate.push_back(TextInputPreeditClauseStyle::Raw);
    }
    for(usize index = suffixStart; index < m_scalarStyles.size(); ++index)
        m_scalarStylesCandidate.push_back(m_scalarStyles[index]);

    m_candidate.clear();
    m_candidate.append(m_text.data(), *firstByte);
    if(!insertion.empty())
        m_candidate.append(insertion.data(), insertion.size());
    m_candidate.append(m_text.data() + lastByte, m_text.size() - lastByte);
    const auto caretByte = Utf8ByteOffset(m_candidate, caretCharacter);
    if(!caretByte)
        return false;

    const b8 hasFeedback = m_hasFeedback || !insertionStyles.empty();
    m_clauses.clear();
    if(hasFeedback && !m_candidate.empty() && !rebuildClauses(m_candidate, Span<const TextInputPreeditClauseStyle::Enum>(m_scalarStylesCandidate.data(), m_scalarStylesCandidate.size())))
        return false;

    m_hasFeedback = hasFeedback;
    m_text.swap(m_candidate);
    m_scalarStyles.swap(m_scalarStylesCandidate);
    m_caretByte = *caretByte;
    return true;
}

b8 X11PreeditBuffer::updateFeedback(const usize firstCharacter, const usize characterCount, const Span<const TextInputPreeditClauseStyle::Enum> styles, const usize caretCharacter)noexcept{
    if(characterCount > m_scalarStyles.size() || firstCharacter > m_scalarStyles.size() - characterCount)
        return false;
    if(styles.size() != characterCount)
        return false;
    const auto caretByte = Utf8ByteOffset(m_text, caretCharacter);
    if(!caretByte)
        return false;
    for(usize index = 0u; index < characterCount; ++index)
        m_scalarStyles[firstCharacter + index] = styles[index];
    m_hasFeedback = true;
    m_clauses.clear();
    if(!m_text.empty() && !rebuildClauses(m_text, Span<const TextInputPreeditClauseStyle::Enum>(m_scalarStyles.data(), m_scalarStyles.size())))
        return false;
    m_caretByte = *caretByte;
    return true;
}

b8 X11PreeditBuffer::rebuildClauses(const AStringView text, const Span<const TextInputPreeditClauseStyle::Enum> styles){
    m_clauses.reserve(styles.size());
    usize byteOffset = 0u;
    usize scalarIndex = 0u;
    TextInputPreeditClause activeClause;
    b8 hasActiveClause = false;
    while(byteOffset < text.size() && scalarIndex < styles.size()){
        const auto decoded = DecodeUtf8Scalar(text.substr(byteOffset));
        if(!decoded || decoded->byteCount == 0u)
            return false;
        const usize nextByte = byteOffset + static_cast<usize>(decoded->byteCount);
        const TextInputPreeditClauseStyle::Enum style = styles[scalarIndex];
        if(hasActiveClause && activeClause.style == style && activeClause.endByte == byteOffset){
            activeClause.endByte = nextByte;
        }
        else{
            if(hasActiveClause)
                m_clauses.push_back(activeClause);
            activeClause.beginByte = byteOffset;
            activeClause.endByte = nextByte;
            activeClause.style = style;
            hasActiveClause = true;
        }
        byteOffset = nextByte;
        ++scalarIndex;
    }
    if(hasActiveClause)
        m_clauses.push_back(activeClause);
    return true;
}

b8 X11PreeditBuffer::moveCaret(const usize caretCharacter)noexcept{
    const auto caretByte = Utf8ByteOffset(m_text, caretCharacter);
    if(!caretByte)
        return false;
    m_caretByte = *caretByte;
    return true;
}

usize X11PreeditBuffer::moveCaretToEnd()noexcept{
    m_caretByte = m_text.size();
    return CountUtf8ScalarsAssumingValid(AStringView(m_text));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
