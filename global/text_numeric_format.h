// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "simplemath.h"

#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller-owned bounded storage keeps scalar formatting independent of an allocator or locale.
template<usize N>
[[nodiscard]] inline AStringView FormatI64(const i64 value, char (&buffer)[N]){
    const auto result = std::to_chars(buffer, buffer + N, value);
    if(result.ec != std::errc())
        return {};
    return { buffer, static_cast<usize>(result.ptr - buffer) };
}

// The shortest general representation roundtrips to the same finite double, including signed zero.
template<usize N>
[[nodiscard]] inline AStringView FormatF64(const f64 value, char (&buffer)[N]){
    if(!IsFinite(value))
        return {};
    const auto result = std::to_chars(buffer, buffer + N, value);
    if(result.ec != std::errc())
        return {};
    return { buffer, static_cast<usize>(result.ptr - buffer) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

