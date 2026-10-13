// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_text.h"
#include "utf8_text_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum ValidateTextInputUtf8(const AStringView text, const usize maxBytes)noexcept{
    if(text.size() > maxBytes)
        return TextInputAdmission::TooLarge;
    return Utf8TextDetail::IsValidText(text) ? TextInputAdmission::Accepted : TextInputAdmission::InvalidText;
}

bool IsTextInputUtf8Boundary(const AStringView text, const usize byteOffset)noexcept{
    return byteOffset <= text.size() && (byteOffset == text.size() || (static_cast<u8>(text[byteOffset]) & 0xc0u) != 0x80u);
}

bool IsTextInputCaretRectValid(const TextInputRect rect)noexcept{
    using CoordinatePair = i32 __attribute__((ext_vector_type(2)));
    const CoordinatePair extent = { rect.width, rect.height };
    if(!__builtin_reduce_and(extent > CoordinatePair{ 0, 0 }))
        return false;
    const CoordinatePair maximum = { Limit<i32>::s_Max, Limit<i32>::s_Max };
    const CoordinatePair origin = { rect.x, rect.y };
    return __builtin_reduce_and(origin <= maximum - extent);
}

Expected<usize, TextInputAdmission::Enum> EncodeTextInputCodePoint(const u32 codePoint, char (&bytes)[4])noexcept{
    usize length = 0u;
    if(codePoint == 0u || codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        return MakeUnexpected(TextInputAdmission::InvalidText);
    if(codePoint < 0x80u){
        bytes[0] = static_cast<char>(codePoint);
        length = 1u;
    }
    else if(codePoint < 0x800u){
        bytes[0] = static_cast<char>(0xc0u | (codePoint >> 6u));
        bytes[1] = static_cast<char>(0x80u | (codePoint & 0x3fu));
        length = 2u;
    }
    else if(codePoint < 0x10000u){
        bytes[0] = static_cast<char>(0xe0u | (codePoint >> 12u));
        bytes[1] = static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu));
        bytes[2] = static_cast<char>(0x80u | (codePoint & 0x3fu));
        length = 3u;
    }
    else{
        bytes[0] = static_cast<char>(0xf0u | (codePoint >> 18u));
        bytes[1] = static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3fu));
        bytes[2] = static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3fu));
        bytes[3] = static_cast<char>(0x80u | (codePoint & 0x3fu));
        length = 4u;
    }
    return length;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

