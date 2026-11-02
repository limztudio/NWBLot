// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_text.h"

#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum ValidateTextInputUtf8(const AStringView text, const usize maxBytes)noexcept{
    if(text.size() > maxBytes)
        return TextInputAdmission::TooLarge;
    return IsValidUtf8TextWithoutNul(text) ? TextInputAdmission::Accepted : TextInputAdmission::InvalidText;
}

b8 IsTextInputCaretRectValid(const TextInputRect rect)noexcept{
    if(rect.width <= 0 || rect.height <= 0)
        return false;
    return rect.x <= Limit<i32>::s_Max - rect.width && rect.y <= Limit<i32>::s_Max - rect.height;
}

Expected<usize, TextInputAdmission::Enum> EncodeTextInputCodePoint(const u32 codePoint, Array<char, s_Utf8MaxSequenceBytes>& bytes)noexcept{
    if(codePoint == 0u)
        return MakeUnexpected(TextInputAdmission::InvalidText);
    const auto encoded = EncodeUtf8CodePoint(codePoint, bytes);
    if(!encoded)
        return MakeUnexpected(TextInputAdmission::InvalidText);
    return *encoded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

