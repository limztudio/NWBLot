// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"
#include "bit.h"
#include "not_null.h"
#include "termination.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
inline void AppendU32LittleEndian(Container& bytes, const u32 value)noexcept(noexcept(bytes.push_back(DeclVal<typename Container::value_type>()))){
    BinaryDetail::RequireByteContainer<Container>();
    for(u32 index = 0u; index < sizeof(u32); ++index)
        bytes.push_back(static_cast<typename Container::value_type>(value >> (index * s_BitsPerByte)));
}

template<typename Container>
inline void AppendF32LittleEndian(Container& bytes, const f32 value)noexcept(noexcept(AppendU32LittleEndian(bytes, BitCast<u32>(value)))){
    AppendU32LittleEndian(bytes, BitCast<u32>(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
inline void WriteF32LittleEndianAt(const f32 value, const usize offset, Container& outBytes)noexcept(
    noexcept(static_cast<b8>(offset + 4u > outBytes.size()))
    && noexcept(outBytes[offset] = u8{})
    && noexcept(outBytes[offset + 1u] = u8{})
){
    if(offset + 4u > outBytes.size())
        return;
    if(offset > Limit<usize>::s_Max - 4u)
        TerminateInvariant();
    const u32 word = BitCast<u32>(value);
    outBytes[offset] = static_cast<u8>(word & 0xffu);
    outBytes[offset + 1u] = static_cast<u8>((word >> 8u) & 0xffu);
    outBytes[offset + 2u] = static_cast<u8>((word >> 16u) & 0xffu);
    outBytes[offset + 3u] = static_cast<u8>((word >> 24u) & 0xffu);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller has admitted four readable bytes.
[[nodiscard]] inline u32 ReadU32LittleEndian(const NotNull<const u8*> bytes)noexcept{
    return static_cast<u32>(bytes.get()[0u]) | (static_cast<u32>(bytes.get()[1u]) << s_BitsPerByte) | (static_cast<u32>(bytes.get()[2u]) << (s_BitsPerByte * 2u)) | (static_cast<u32>(bytes.get()[3u]) << (s_BitsPerByte * 3u));
}

template<typename Container>
[[nodiscard]] inline Expected<u32> ReadU32LittleEndian(const Container& bytes, usize& cursor)noexcept(noexcept(CanReadBytes(bytes, cursor, sizeof(u32))) && IsPointer_V<decltype(bytes.data())> && noexcept(bytes.data())){
    BinaryDetail::RequireByteContainer<Container>();
    if(!CanReadBytes(bytes, cursor, sizeof(u32)))
        return MakeUnexpected(Failure{});
    const u32 value = ReadU32LittleEndian(NotNull<const u8*>(reinterpret_cast<const u8*>(bytes.data()) + cursor));
    cursor += sizeof(u32);
    return value;
}

template<typename Container>
[[nodiscard]] inline Expected<f32> ReadF32LittleEndian(const Container& bytes, usize& cursor)noexcept(noexcept(ReadU32LittleEndian(bytes, cursor))){
    const auto value = ReadU32LittleEndian(bytes, cursor);
    if(!value)
        return MakeUnexpected(value.error());
    return BitCast<f32>(*value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

