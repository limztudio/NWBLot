// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Utf8TextDetail{
// 0xc0 and 0xc1 would start overlong forms and leads above 0xf4 pass U+10FFFF.
inline constexpr u8 s_TwoByteLeadMin = 0xc2u;
inline constexpr u8 s_TwoByteLeadMax = 0xdfu;
inline constexpr u8 s_ThreeByteLeadMin = 0xe0u;
inline constexpr u8 s_ThreeByteLeadMax = 0xefu;
inline constexpr u8 s_FourByteLeadMin = 0xf0u;
inline constexpr u8 s_FourByteLeadMax = 0xf4u;

[[nodiscard]] inline Expected<u32> DecodeCodePoint(const AStringView text, usize& offset)noexcept{
    u32 codePoint = 0u;
    const u8 first = static_cast<u8>(text[offset]);
    ++offset;
    if(first < BasicStringDetail::s_Utf8OneByteMaxExclusive){
        codePoint = first;
        return first != 0u ? Expected<u32>(first) : MakeUnexpected(Failure{});
    }
    usize continuation = 0u;
    u32 minimum = 0u;
    if(first >= s_TwoByteLeadMin && first <= s_TwoByteLeadMax){
        continuation = 1u;
        codePoint = first & BasicStringDetail::s_Utf8TwoBytePayloadMask;
        minimum = BasicStringDetail::s_Utf8TwoByteMinCodePoint;
    }
    else if(first >= s_ThreeByteLeadMin && first <= s_ThreeByteLeadMax){
        continuation = 2u;
        codePoint = first & BasicStringDetail::s_Utf8ThreeBytePayloadMask;
        minimum = BasicStringDetail::s_Utf8ThreeByteMinCodePoint;
    }
    else if(first >= s_FourByteLeadMin && first <= s_FourByteLeadMax){
        continuation = 3u;
        codePoint = first & BasicStringDetail::s_Utf8FourBytePayloadMask;
        minimum = BasicStringDetail::s_Utf8FourByteMinCodePoint;
    }
    else
        return MakeUnexpected(Failure{});
    if(continuation > text.size() - offset)
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < continuation; ++index){
        const u8 byte = static_cast<u8>(text[offset]);
        if((byte & BasicStringDetail::s_Utf8ContinuationMask) != BasicStringDetail::s_Utf8ContinuationMarker)
            return MakeUnexpected(Failure{});
        codePoint = (codePoint << BasicStringDetail::s_Utf8ContinuationPayloadBits) | (byte & BasicStringDetail::s_Utf8ContinuationPayloadMask);
        ++offset;
    }
    if(codePoint < minimum || codePoint > BasicStringDetail::s_UnicodeMaxCodePoint || (codePoint >= BasicStringDetail::s_UnicodeHighSurrogateMin && codePoint <= BasicStringDetail::s_UnicodeLowSurrogateMax))
        return MakeUnexpected(Failure{});
    return codePoint;
}

[[nodiscard]] inline bool IsValidText(const AStringView text)noexcept{
    usize offset = 0u;
    while(offset < text.size()){
        if(!DecodeCodePoint(text, offset))
            return false;
    }
    return true;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

