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

bool IsTextInputCaretRectValid(const TextInputRect rect)noexcept{
    using CoordinatePair = i32 __attribute__((ext_vector_type(2)));
    const CoordinatePair extent = { rect.width, rect.height };
    if(!__builtin_reduce_and(extent > CoordinatePair{ 0, 0 }))
        return false;
    const CoordinatePair maximum = { Limit<i32>::s_Max, Limit<i32>::s_Max };
    const CoordinatePair origin = { rect.x, rect.y };
    return __builtin_reduce_and(origin <= maximum - extent);
}

Expected<usize, TextInputAdmission::Enum> EncodeTextInputCodePoint(const u32 codePoint, char (&bytes)[s_Utf8MaxSequenceBytes])noexcept{
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

