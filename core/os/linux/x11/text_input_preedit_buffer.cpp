// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_preedit.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_preedit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<usize> ByteOffset(const AStringView text, const usize character)noexcept{
    usize position = 0u;
    usize index = 0u;
    while(position < text.size()){
        if(index == character){
            return position;
        }
        const auto decoded = DecodeUtf8CodePoint(text.substr(position));
        if(!decoded || decoded->codePoint == 0u || (decoded->codePoint >= 0xD800u && decoded->codePoint <= 0xDFFFu))
            return MakeUnexpected(Failure{});
        position += static_cast<usize>(decoded->byteCount);
        ++index;
    }
    if(index != character)
        return MakeUnexpected(Failure{});
    return position;
}

[[nodiscard]] static bool ValidUtf8(const AStringView text)noexcept{
    if(text.size() > X11PreeditBuffer::s_MaxBytes)
        return false;
    usize position = 0u;
    while(position < text.size()){
        const auto decoded = DecodeUtf8CodePoint(text.substr(position));
        if(!decoded || decoded->codePoint == 0u || (decoded->codePoint >= 0xD800u && decoded->codePoint <= 0xDFFFu))
            return false;
        position += static_cast<usize>(decoded->byteCount);
    }
    return true;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11PreeditBuffer::X11PreeditBuffer(Alloc::GlobalArena& arena)
    : m_text(arena)
    , m_candidate(arena)
{}

void X11PreeditBuffer::clear()noexcept{
    m_text.clear();
    m_candidate.clear();
    m_caretByte = 0u;
}

bool X11PreeditBuffer::replace(
    const usize firstCharacter,
    const usize characterCount,
    const AStringView insertion,
    const usize caretCharacter
){
    using namespace __hidden_x11_preedit;
    if(characterCount > s_MaxBytes || firstCharacter > s_MaxBytes - characterCount)
        return false;
    if(!ValidUtf8(insertion))
        return false;
    const auto firstByte = ByteOffset(m_text, firstCharacter);
    if(!firstByte)
        return false;
    const auto lastByte = ByteOffset(m_text, firstCharacter + characterCount);
    if(!lastByte)
        return false;
    const usize remainingBytes = m_text.size() - (*lastByte - *firstByte);
    if(insertion.size() > s_MaxBytes - remainingBytes)
        return false;
    m_candidate.clear();
    m_candidate.append(m_text.data(), *firstByte);
    if(!insertion.empty())
        m_candidate.append(insertion.data(), insertion.size());
    m_candidate.append(m_text.data() + *lastByte, m_text.size() - *lastByte);
    const auto caretByte = ByteOffset(m_candidate, caretCharacter);
    if(!caretByte)
        return false;
    m_text.swap(m_candidate);
    m_caretByte = *caretByte;
    return true;
}

bool X11PreeditBuffer::moveCaret(const usize caretCharacter)noexcept{
    const auto caretByte = __hidden_x11_preedit::ByteOffset(m_text, caretCharacter);
    if(!caretByte)
        return false;
    m_caretByte = *caretByte;
    return true;
}

usize X11PreeditBuffer::moveCaretToEnd()noexcept{
    usize characters = 0u;
    for(const char byte : m_text){
        if(!IsUtf8Continuation(static_cast<u8>(byte)))
            ++characters;
    }
    m_caretByte = m_text.size();
    return characters;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

