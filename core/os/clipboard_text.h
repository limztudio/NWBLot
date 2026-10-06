// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] ClipboardStatus::Enum ValidateClipboardUtf8Text(AStringView text)noexcept;
[[nodiscard]] ClipboardStatus::Enum DecodeClipboardLatin1(AStringView text, AString<Alloc::GlobalArena>& output);
[[nodiscard]] ClipboardStatus::Enum EncodeClipboardLatin1(AStringView text, AString<Alloc::GlobalArena>& output);


// Native transfer chunks may split a UTF-8 sequence. Validate encoding only after the complete byte stream arrives.
class ClipboardTextAccumulator final{
public:
    explicit ClipboardTextAccumulator(Alloc::GlobalArena& arena);


public:
    void clear()noexcept;
    [[nodiscard]] ClipboardStatus::Enum appendBytes(AStringView bytes);
    [[nodiscard]] const AString<Alloc::GlobalArena>& text()const noexcept{ return m_text; }


private:
    AString<Alloc::GlobalArena> m_text;
    ClipboardStatus::Enum m_status = ClipboardStatus::Success;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

