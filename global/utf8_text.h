// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_trim_parse.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool IsValidUtf8TextWithoutNul(const AStringView text)noexcept{
    usize offset = 0u;
    while(offset < text.size()){
        const auto decoded = DecodeUtf8CodePoint(text.substr(offset));
        if(!decoded || decoded->codePoint == 0u || (decoded->codePoint >= BasicStringDetail::s_UnicodeHighSurrogateMin && decoded->codePoint <= BasicStringDetail::s_UnicodeLowSurrogateMax))
            return false;
        offset += static_cast<usize>(decoded->byteCount);
    }
    return true;
}

// The requested prefix is validated; bytes after the boundary are not read.
[[nodiscard]] inline Expected<usize> Utf8ByteOffset(const AStringView text, const usize character)noexcept{
    usize position = 0u;
    usize index = 0u;
    while(position < text.size()){
        if(index == character){
            return position;
        }
        const auto decoded = DecodeUtf8CodePoint(text.substr(position));
        if(!decoded || decoded->codePoint == 0u || (decoded->codePoint >= BasicStringDetail::s_UnicodeHighSurrogateMin && decoded->codePoint <= BasicStringDetail::s_UnicodeLowSurrogateMax))
            return MakeUnexpected(Failure{});
        position += static_cast<usize>(decoded->byteCount);
        ++index;
    }
    if(index != character)
        return MakeUnexpected(Failure{});
    return position;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

