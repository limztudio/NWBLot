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
    usize offset = 0u;
    while(offset < text.size()){
        u32 codePoint = 0u;
        if(!Utf8TextDetail::DecodeCodePoint(text, offset, codePoint))
            return TextInputAdmission::InvalidText;
    }
    return TextInputAdmission::Accepted;
}

bool IsTextInputUtf8Boundary(const AStringView text, const usize byteOffset)noexcept{
    return byteOffset <= text.size()
        && (byteOffset == text.size() || (static_cast<u8>(text[byteOffset]) & 0xc0u) != 0x80u)
    ;
}

bool IsTextInputCaretRectValid(const TextInputRect rect)noexcept{
    return rect.width > 0 && rect.height > 0
        && rect.x <= Limit<i32>::s_Max - rect.width && rect.y <= Limit<i32>::s_Max - rect.height
    ;
}

TextInputAdmission::Enum EncodeTextInputCodePoint(const u32 codePoint, char (&bytes)[4], usize& length)noexcept{
    length = 0u;
    if(codePoint == 0u || codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        return TextInputAdmission::InvalidText;
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
    return TextInputAdmission::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

