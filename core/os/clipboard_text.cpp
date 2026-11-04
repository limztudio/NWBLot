// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_text.h"

#include <global/latin1_text.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] ClipboardStatus::Enum ToClipboardStatus(const Expected<void, Latin1TextError::Enum>& result)noexcept{
    if(result)
        return ClipboardStatus::Success;
    switch(result.error()){
    case Latin1TextError::InvalidText: return ClipboardStatus::InvalidText;
    case Latin1TextError::TooLarge: return ClipboardStatus::TooLarge;
    case Latin1TextError::Unsupported: return ClipboardStatus::Unsupported;
    }
    return ClipboardStatus::InvalidText;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardStatus::Enum ValidateClipboardUtf8Text(const AStringView text)noexcept{
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    return IsValidUtf8TextWithoutNul(text) ? ClipboardStatus::Success : ClipboardStatus::InvalidText;
}

ClipboardStatus::Enum DecodeClipboardLatin1(const AStringView text, AString<Alloc::GlobalArena>& output){
    return __hidden_clipboard_text::ToClipboardStatus(DecodeLatin1Text(text, s_ClipboardMaxTextBytes, output));
}

ClipboardStatus::Enum EncodeClipboardLatin1(const AStringView text, AString<Alloc::GlobalArena>& output){
    return __hidden_clipboard_text::ToClipboardStatus(EncodeLatin1Text(text, s_ClipboardMaxTextBytes, output));
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

