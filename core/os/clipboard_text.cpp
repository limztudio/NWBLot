// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text{
static bool DecodeCodePoint(const AStringView text, usize& offset, u32& codePoint){
    const u8 first = static_cast<u8>(text[offset]);
    ++offset;
    if(first < 0x80u){
        codePoint = first;
        return first != 0u;
    }
    usize continuation = 0u;
    u32 minimum = 0u;
    if(first >= 0xc2u && first <= 0xdfu){
        continuation = 1u;
        codePoint = first & 0x1fu;
        minimum = 0x80u;
    }
    else if(first >= 0xe0u && first <= 0xefu){
        continuation = 2u;
        codePoint = first & 0x0fu;
        minimum = 0x800u;
    }
    else if(first >= 0xf0u && first <= 0xf4u){
        continuation = 3u;
        codePoint = first & 0x07u;
        minimum = 0x10000u;
    }
    else
        return false;
    if(continuation > text.size() - offset)
        return false;
    for(usize index = 0u; index < continuation; ++index){
        const u8 byte = static_cast<u8>(text[offset]);
        if((byte & 0xc0u) != 0x80u)
            return false;
        codePoint = (codePoint << 6u) | (byte & 0x3fu);
        ++offset;
    }
    return codePoint >= minimum && codePoint <= 0x10ffffu && (codePoint < 0xd800u || codePoint > 0xdfffu);
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardStatus::Enum ValidateClipboardUtf8Text(const AStringView text){
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    usize offset = 0u;
    while(offset < text.size()){
        u32 codePoint = 0u;
        if(!__hidden_clipboard_text::DecodeCodePoint(text, offset, codePoint))
            return ClipboardStatus::InvalidText;
    }
    return ClipboardStatus::Success;
}

ClipboardStatus::Enum DecodeClipboardLatin1(const AStringView text, AString<Alloc::GlobalArena>& output){
    output.clear();
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    usize outputSize = text.size();
    for(const char character : text){
        if(character == '\0')
            return ClipboardStatus::InvalidText;
        if(static_cast<u8>(character) >= 0x80u)
            ++outputSize;
    }
    if(outputSize > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    output.reserve(outputSize);
    for(const char character : text){
        const u8 byte = static_cast<u8>(character);
        if(byte >= 0x80u){
            output.push_back(static_cast<char>(0xc0u | (byte >> 6u)));
            output.push_back(static_cast<char>(0x80u | (byte & 0x3fu)));
        }
        else
            output.push_back(character);
    }
    return ClipboardStatus::Success;
}

ClipboardStatus::Enum EncodeClipboardLatin1(const AStringView text, AString<Alloc::GlobalArena>& output){
    output.clear();
    const ClipboardStatus::Enum status = ValidateClipboardUtf8Text(text);
    if(status != ClipboardStatus::Success)
        return status;
    usize offset = 0u;
    while(offset < text.size()){
        u32 codePoint = 0u;
        if(!__hidden_clipboard_text::DecodeCodePoint(text, offset, codePoint))
            return ClipboardStatus::InvalidText;
        if(codePoint > 0xffu){
            output.clear();
            return ClipboardStatus::Unsupported;
        }
        output.push_back(static_cast<char>(codePoint));
    }
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardTextAccumulator::ClipboardTextAccumulator(Alloc::GlobalArena& arena)
    : m_text(arena)
{}

void ClipboardTextAccumulator::clear(){
    m_text.clear();
    m_status = ClipboardStatus::Success;
}

ClipboardStatus::Enum ClipboardTextAccumulator::appendBytes(const AStringView bytes){
    if(m_status != ClipboardStatus::Success)
        return m_status;
    if(bytes.size() > s_ClipboardMaxTextBytes - m_text.size())
        m_status = ClipboardStatus::TooLarge;
    else if(bytes.find('\0') != AStringView::npos)
        m_status = ClipboardStatus::InvalidText;
    if(m_status != ClipboardStatus::Success){
        m_text.clear();
        return m_status;
    }
    if(!bytes.empty())
        m_text.append(bytes.data(), bytes.size());
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

