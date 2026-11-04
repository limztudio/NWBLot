// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "basic_string.h"
#include "numeric_text.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FixedBufferDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void CopyBuffer(char* const dst, const usize capacity, const AStringView src)noexcept{
    if(capacity == 0u)
        return;

    const usize count = src.size() < capacity - 1u ? src.size() : capacity - 1u;
    for(usize i = 0u; i < count; ++i)
        dst[i] = src[i];
    dst[count] = 0;
}

inline void AppendBuffer(char* const dst, const usize capacity, const AStringView src)noexcept{
    if(capacity == 0u)
        return;

    usize len = 0u;
    while(len + 1u < capacity && dst[len] != 0)
        ++len;

    usize i = 0u;
    while(len + 1u < capacity && i < src.size()){
        dst[len] = src[i];
        ++len;
        ++i;
    }
    dst[len] = 0;
}

inline void AppendUnsignedBuffer(char* const dst, const usize capacity, u64 value)noexcept{
    Array<char, s_DecimalTextBufferBytes> text;
    AppendBuffer(dst, capacity, TextDetail::FormatDecimalBuffer(value, text.data()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize N>
[[nodiscard]] inline constexpr AStringView FixedBufferView(const Array<char, N>& buffer)noexcept{
    usize len = 0u;
    while(len < N && buffer[len] != 0)
        ++len;
    return AStringView(buffer.data(), len);
}

template<usize N>
inline void CopyFixedBuffer(Array<char, N>& dst, const AStringView src)noexcept{
    FixedBufferDetail::CopyBuffer(dst.data(), N, src);
}

template<usize N, usize M>
inline void CopyFixedBuffer(Array<char, N>& dst, const Array<char, M>& src)noexcept{
    CopyFixedBuffer(dst, FixedBufferView(src));
}

template<usize N>
inline void CopyFixedBuffer(char (&dst)[N], const AStringView src)noexcept{
    FixedBufferDetail::CopyBuffer(dst, N, src);
}

template<usize N, usize M>
inline void CopyFixedBuffer(char (&dst)[N], const Array<char, M>& src)noexcept{
    CopyFixedBuffer(dst, FixedBufferView(src));
}

template<usize N>
inline void AppendFixedBuffer(Array<char, N>& dst, const AStringView src)noexcept{
    FixedBufferDetail::AppendBuffer(dst.data(), N, src);
}

template<usize N>
inline void AppendFixedBuffer(char (&dst)[N], const AStringView src)noexcept{
    FixedBufferDetail::AppendBuffer(dst, N, src);
}

template<usize N>
inline void AppendUnsignedToFixedBuffer(Array<char, N>& dst, const u64 value)noexcept{
    FixedBufferDetail::AppendUnsignedBuffer(dst.data(), N, value);
}

template<usize N>
inline void AppendUnsignedToFixedBuffer(char (&dst)[N], const u64 value)noexcept{
    FixedBufferDetail::AppendUnsignedBuffer(dst, N, value);
}

template<typename StringT>
inline void AppendUnsignedU64Text(StringT& out, const u64 value)noexcept(noexcept(out += AStringView{})){
    Array<char, s_DecimalTextBufferBytes> buffer = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    out += FixedBufferView(buffer);
}

template<typename ByteContainer>
inline void AppendUnsignedU64Bytes(ByteContainer& out, const u64 value)noexcept(noexcept(out.insert(out.end(), DeclVal<const char*>(), DeclVal<const char*>()))){
    Array<char, s_DecimalTextBufferBytes> buffer = {};
    AppendUnsignedToFixedBuffer(buffer, value);
    const AStringView text = FixedBufferView(buffer);
    out.insert(out.end(), text.begin(), text.end());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

