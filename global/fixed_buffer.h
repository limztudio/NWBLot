// limztudio@gmail.com


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "numeric_text.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
inline void AppendFixedBuffer(char (&dst)[N], const AStringView src)noexcept{
    if constexpr(N == 0u)
        return;

    usize len = 0u;
    while(len + 1u < N && dst[len] != 0)
        ++len;

    usize i = 0u;
    while(len + 1u < N && i < src.size()){
        dst[len] = src[i];
        ++len;
        ++i;
    }
    dst[len] = 0;
}

template<usize N>
inline void AppendUnsignedToFixedBuffer(char (&dst)[N], u64 value)noexcept{
    char text[TextDetail::s_DecimalTextBufferBytes];
    usize begin = sizeof(text);
    do{
        text[--begin] = static_cast<char>('0' + (value % TextDetail::s_DefaultNumericTextBase));
        value /= TextDetail::s_DefaultNumericTextBase;
    }while(value && begin != 0u);

    AppendFixedBuffer(dst, AStringView(text + begin, sizeof(text) - begin));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

