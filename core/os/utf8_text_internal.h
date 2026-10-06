// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Utf8TextDetail{
[[nodiscard]] inline bool DecodeCodePoint(const AStringView text, usize& offset, u32& codePoint)noexcept{
    const u8 first = static_cast<u8>(text[offset]);
    ++offset;
    if(first < 0x80u){
        codePoint = first;
        return first != 0u;
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
        return false;
    if(continuation > text.size() - offset)
        return false;
    for(usize index = 0u; index < continuation; ++index){
        const u8 byte = static_cast<u8>(text[offset]);
        if((byte & 0xc0u) != 0x80u)
            return false;
        codePoint = (codePoint << 6u) | (byte & 0x3fu);
        ++offset;
    }
    return codePoint >= minimum && codePoint <= 0x10ffffu && (codePoint < 0xd800u || codePoint > 0xdfffu);
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

