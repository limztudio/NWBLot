// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "limit.h"
#include "not_null.h"
#include "span.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller has admitted two readable bytes.
[[nodiscard]] inline u16 ReadU16BigEndian(const NotNull<const u8*> bytes)noexcept{
    const u8* const data = bytes.get();
    return static_cast<u16>((static_cast<u16>(data[0u]) << s_BitsPerByte) | data[1u]);
}

[[nodiscard]] inline Expected<u16> ReadU16BigEndian(const Span<const u8> bytes, const usize offset = 0u)noexcept{
    if(offset > bytes.size() || bytes.size() - offset < sizeof(u16))
        return MakeUnexpected(Failure{});
    return ReadU16BigEndian(NotNull<const u8*>(bytes.data() + offset));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caller has admitted four readable bytes.
[[nodiscard]] inline u32 ReadU32BigEndian(const NotNull<const u8*> bytes)noexcept{
    const u8* const data = bytes.get();
    return (static_cast<u32>(data[0u]) << (s_BitsPerByte * 3u)) | (static_cast<u32>(data[1u]) << (s_BitsPerByte * 2u)) | (static_cast<u32>(data[2u]) << s_BitsPerByte) | static_cast<u32>(data[3u]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

