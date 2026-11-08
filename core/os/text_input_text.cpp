// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_text.h"

#include "text_input_validation.adhd.h"

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
    static_assert(s_MaxI32 == Limit<i32>::s_Max);
    return ValidTextInputCaretCoordinates(rect.x, rect.y, rect.width, rect.height);
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

