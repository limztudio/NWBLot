// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_preedit.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_preedit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ByteOffset(const AStringView text, const usize character, usize& offset)noexcept{
    usize position = 0u;
    usize index = 0u;
    while(position < text.size()){
        if(index == character){
            offset = position;
            return true;
        }
        u32 codePoint = 0u;
        const i32 bytes = DecodeUtf8CodePoint(text.substr(position), codePoint);
        if(bytes <= 0 || codePoint == 0u || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
            return false;
        position += static_cast<usize>(bytes);
        ++index;
    }
    if(index != character)
        return false;
    offset = position;
    return true;
}

[[nodiscard]] static bool ValidUtf8(const AStringView text)noexcept{
    if(text.size() > X11PreeditBuffer::s_MaxBytes)
        return false;
    usize position = 0u;
    while(position < text.size()){
        u32 codePoint = 0u;
        const i32 bytes = DecodeUtf8CodePoint(text.substr(position), codePoint);
        if(bytes <= 0 || codePoint == 0u || (codePoint >= 0xD800u && codePoint <= 0xDFFFu))
            return false;
        position += static_cast<usize>(bytes);
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
    const usize caretCharacter){
    using namespace __hidden_x11_preedit;
    usize firstByte = 0u;
    usize lastByte = 0u;
    if(characterCount > s_MaxBytes || firstCharacter > s_MaxBytes - characterCount)
        return false;
    if(!ValidUtf8(insertion) || !ByteOffset(m_text, firstCharacter, firstByte))
        return false;
    if(!ByteOffset(m_text, firstCharacter + characterCount, lastByte))
        return false;
    const usize remainingBytes = m_text.size() - (lastByte - firstByte);
    if(insertion.size() > s_MaxBytes - remainingBytes)
        return false;
    m_candidate.clear();
    m_candidate.append(m_text.data(), firstByte);
    if(!insertion.empty())
        m_candidate.append(insertion.data(), insertion.size());
    m_candidate.append(m_text.data() + lastByte, m_text.size() - lastByte);
    usize caretByte = 0u;
    if(!ByteOffset(m_candidate, caretCharacter, caretByte))
        return false;
    m_text.swap(m_candidate);
    m_caretByte = caretByte;
    return true;
}

bool X11PreeditBuffer::moveCaret(const usize caretCharacter)noexcept{
    usize caretByte = 0u;
    if(!__hidden_x11_preedit::ByteOffset(m_text, caretCharacter, caretByte))
        return false;
    m_caretByte = caretByte;
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

