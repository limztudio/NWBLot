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
    return byteOffset <= text.size() && (byteOffset == text.size() || (static_cast<u8>(text[byteOffset]) & BasicStringDetail::s_Utf8ContinuationMask) != BasicStringDetail::s_Utf8ContinuationMarker);
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
    if(codePoint == 0u || codePoint > BasicStringDetail::s_UnicodeMaxCodePoint || (codePoint >= BasicStringDetail::s_UnicodeHighSurrogateMin && codePoint <= BasicStringDetail::s_UnicodeLowSurrogateMax))
        return MakeUnexpected(TextInputAdmission::InvalidText);
    if(codePoint < BasicStringDetail::s_Utf8TwoByteMinCodePoint){
        bytes[0] = static_cast<char>(codePoint);
        length = 1u;
    }
    else if(codePoint < BasicStringDetail::s_Utf8ThreeByteMinCodePoint){
        bytes[0] = static_cast<char>(BasicStringDetail::s_Utf8TwoByteMarker | (codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits));
        bytes[1] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        length = 2u;
    }
    else if(codePoint < BasicStringDetail::s_Utf8FourByteMinCodePoint){
        bytes[0] = static_cast<char>(BasicStringDetail::s_Utf8ThreeByteMarker | (codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u)));
        bytes[1] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        bytes[2] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        length = 3u;
    }
    else{
        bytes[0] = static_cast<char>(BasicStringDetail::s_Utf8FourByteMarker | (codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 3u)));
        bytes[1] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> (BasicStringDetail::s_Utf8ContinuationPayloadBits * 2u)) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        bytes[2] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | ((codePoint >> BasicStringDetail::s_Utf8ContinuationPayloadBits) & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        bytes[3] = static_cast<char>(BasicStringDetail::s_Utf8ContinuationMarker | (codePoint & BasicStringDetail::s_Utf8ContinuationPayloadMask));
        length = 4u;
    }
    return length;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

