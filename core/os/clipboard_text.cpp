// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_text.h"

#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text{
static constexpr u32 s_Latin1MaxCodePoint = 0xffu;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardStatus::Enum ValidateClipboardUtf8Text(const AStringView text)noexcept{
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    return IsValidUtf8TextWithoutNul(text) ? ClipboardStatus::Success : ClipboardStatus::InvalidText;
}

ClipboardStatus::Enum DecodeClipboardLatin1(const AStringView text, AString<Alloc::GlobalArena>& output){
    output.clear();
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    usize outputSize = text.size();
    for(const char character : text){
        if(character == '\0')
            return ClipboardStatus::InvalidText;
        if(static_cast<u8>(character) >= BasicStringDetail::s_Utf8OneByteMaxExclusive)
            ++outputSize;
    }
    if(outputSize > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    output.reserve(outputSize);
    for(const char character : text){
        const u8 byte = static_cast<u8>(character);
        if(byte >= BasicStringDetail::s_Utf8OneByteMaxExclusive){
            output.push_back(static_cast<char>(BasicStringDetail::s_Utf8TwoByteMarker | (byte >> BasicStringDetail::s_Utf8ContinuationPayloadBits)));
            output.push_back(static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (byte & BasicStringDetail::s_Utf8ContinuationPayloadMask)));
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
        const auto decoded = DecodeUtf8CodePoint(text.substr(offset));
        if(!decoded)
            return ClipboardStatus::InvalidText;
        if(decoded->codePoint > __hidden_clipboard_text::s_Latin1MaxCodePoint){
            output.clear();
            return ClipboardStatus::Unsupported;
        }
        output.push_back(static_cast<char>(decoded->codePoint));
        offset += static_cast<usize>(decoded->byteCount);
    }
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardTextAccumulator::ClipboardTextAccumulator(Alloc::GlobalArena& arena)noexcept
    : m_text(arena)
{}

void ClipboardTextAccumulator::clear()noexcept{
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

