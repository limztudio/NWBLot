// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/numeric_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::UiCases{

template<typename Number, typename Formatter>
[[nodiscard]] inline Expected<usize, ErrorCode> FormatNumberBytes(const Number value, const Span<u8> buffer, Formatter format)noexcept{
    char empty{};
    char* const data = buffer.empty() ? &empty : reinterpret_cast<char*>(buffer.data());
    const auto result = format(value, Span<char>(data, buffer.size()));
    if(!result)
        return MakeUnexpected(result.error());
    return result->size();
}

[[nodiscard]] inline Expected<usize, ErrorCode> FormatI64Bytes(const i64 value, const Span<u8> buffer)noexcept{
    return FormatNumberBytes(value, buffer, FormatI64);
}

[[nodiscard]] inline Expected<usize, ErrorCode> FormatF64Bytes(const f64 value, const Span<u8> buffer)noexcept{
    return FormatNumberBytes(value, buffer, FormatF64);
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

