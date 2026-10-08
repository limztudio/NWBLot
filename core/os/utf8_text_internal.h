// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Utf8TextDetail{
[[nodiscard]] inline Expected<u32> DecodeCodePoint(const AStringView text, usize& offset)noexcept{
    u32 codePoint = 0u;
    const u8 first = static_cast<u8>(text[offset]);
    ++offset;
    if(first < 0x80u){
        codePoint = first;
        return first != 0u ? Expected<u32>(first) : MakeUnexpected(Failure{});
    }
    usize continuation = 0u;
    u32 minimum = 0u;
    if(first >= 0xc2u && first <= 0xdfu){
        continuation = 1u;
        codePoint = first & 0x1fu;
        minimum = 0x80u;
    }
    else if(first >= 0xe0u && first <= 0xefu){
        continuation = 2u;
        codePoint = first & 0x0fu;
        minimum = 0x800u;
    }
    else if(first >= 0xf0u && first <= 0xf4u){
        continuation = 3u;
        codePoint = first & 0x07u;
        minimum = 0x10000u;
    }
    else
        return MakeUnexpected(Failure{});
    if(continuation > text.size() - offset)
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < continuation; ++index){
        const u8 byte = static_cast<u8>(text[offset]);
        if((byte & 0xc0u) != 0x80u)
            return MakeUnexpected(Failure{});
        codePoint = (codePoint << 6u) | (byte & 0x3fu);
        ++offset;
    }
    if(codePoint < minimum || codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        return MakeUnexpected(Failure{});
    return codePoint;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

