
#pragma once


#include "basic_string.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize N>
inline void CopyFixedBuffer(char (&dst)[N], const char* src)noexcept{
    if constexpr(N == 0u)
        return;
    if(!src){
        dst[0] = 0;
        return;
    }

    usize i = 0u;
    for(; i + 1u < N && src[i] != 0; ++i)
        dst[i] = src[i];
    dst[i] = 0;
}

template<usize N>
inline void CopyFixedBuffer(char (&dst)[N], const AStringView src)noexcept{
    if constexpr(N == 0u)
        return;

    const usize count = src.size() < N - 1u ? src.size() : N - 1u;
    for(usize i = 0u; i < count; ++i)
        dst[i] = src[i];
    dst[count] = 0;
}

template<usize N>
inline void AppendFixedBuffer(char (&dst)[N], const char* src)noexcept{
    if constexpr(N == 0u)
        return;

    usize len = 0u;
    while(len + 1u < N && dst[len] != 0)
        ++len;

    usize i = 0u;
    while(len + 1u < N && src && src[i] != 0)
        dst[len++] = src[i++];
    dst[len] = 0;
}

template<usize N>
inline void AppendUnsignedToFixedBuffer(char (&dst)[N], u64 value)noexcept{
    char tmp[32] = {};
    usize count = 0u;
    do{
        tmp[count++] = static_cast<char>('0' + (value % 10u));
        value /= 10u;
    }while(value && count < sizeof(tmp));

    for(usize i = 0u; i < count; ++i){
        char ch[2] = { tmp[count - i - 1u], 0 };
        AppendFixedBuffer(dst, ch);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

