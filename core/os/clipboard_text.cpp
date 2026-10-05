// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_text.h"
#include "utf8_text_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardStatus::Enum ValidateClipboardUtf8Text(const AStringView text){
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    usize offset = 0u;
    while(offset < text.size()){
        u32 codePoint = 0u;
        if(!Utf8TextDetail::DecodeCodePoint(text, offset, codePoint))
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
        if(!Utf8TextDetail::DecodeCodePoint(text, offset, codePoint))
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

