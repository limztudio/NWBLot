// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "expected.h"
#include "error_code.h"
#include "limit.h"
#include "simplemath.h"

#include <cerrno>
#include <charconv>
#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DecimalTextBufferBytes = 32u;
inline constexpr i32 s_DefaultNumericTextBase = 10;
inline constexpr usize s_UsizeDecimalDigitCount = []()noexcept{
    usize digits = 1u;
    for(usize value = Limit<usize>::s_Max; value >= 10u; value /= 10u)
        ++digits;
    return digits;
}();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_HexDecimalDigitCount = 10u;
inline constexpr usize s_HexPrefixLength = 2u;
inline constexpr usize s_HexU32DigitCount = 8u;
inline constexpr usize s_HexU64DigitCount = 16u;
inline constexpr u32 s_HexNibbleBits = 4u;
inline constexpr u64 s_HexNibbleMask = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller-owned bounded storage keeps scalar formatting independent of an allocator or locale.
template<usize N>
[[nodiscard]] inline Expected<AStringView, ErrorCode> FormatI64(const i64 value, char (&buffer)[N])noexcept{
    const auto result = std::to_chars(buffer, buffer + N, value);
    if(result.ec != ErrorCondition{})
        return MakeUnexpected(MakeErrorCode(result.ec));
    return AStringView(buffer, static_cast<usize>(result.ptr - buffer));
}

// The shortest general representation roundtrips to the same finite double, including signed zero.
template<usize N>
[[nodiscard]] inline Expected<AStringView, ErrorCode> FormatF64(const f64 value, char (&buffer)[N])noexcept{
    if(!IsFinite(value))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    const auto result = std::to_chars(buffer, buffer + N, value);
    if(result.ec != ErrorCondition{})
        return MakeUnexpected(MakeErrorCode(result.ec));
    return AStringView(buffer, static_cast<usize>(result.ptr - buffer));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Full-range caller storage makes decimal formatting total, including zero and the largest usize.
template<usize N>
    requires (N >= TextDetail::s_UsizeDecimalDigitCount)
[[nodiscard]] inline constexpr AStringView FormatDecimal(const usize value, char (&buffer)[N])noexcept{
    usize remaining = value;
    usize length = 0u;
    do{
        buffer[length] = static_cast<char>('0' + remaining % 10u);
        ++length;
        remaining /= 10u;
    }while(remaining != 0u);
    for(usize left = 0u, right = length - 1u; left < right; ++left, --right){
        const char digit = buffer[left];
        buffer[left] = buffer[right];
        buffer[right] = digit;
    }
    return AStringView(buffer, length);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline std::ios_base& StreamHex(std::ios_base& stream)noexcept(noexcept(stream.setf(std::ios_base::hex, std::ios_base::basefield))){ return std::hex(stream); }

inline std::ios_base& StreamDec(std::ios_base& stream)noexcept(noexcept(stream.setf(std::ios_base::dec, std::ios_base::basefield))){ return std::dec(stream); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// `consumed` counts every character used by the parsed prefix, including leading space.
template<typename NumberT>
struct NumericTextPrefix{
    NumberT value = NumberT(0);
    usize consumed = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextParseDetail{


template<typename NumberT>
[[nodiscard]] inline Expected<NumberT> ParseNumber(const AStringView text)noexcept(IsArithmetic_V<NumberT>){
    if(text.empty())
        return MakeUnexpected(Failure{});

    NumberT value = NumberT(0);
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto parsed = [&]()noexcept(IsArithmetic_V<NumberT>){
        if constexpr(IsIntegral_V<NumberT>)
            return std::from_chars(begin, end, value, TextDetail::s_DefaultNumericTextBase);
        else
            return std::from_chars(begin, end, value);
    }();
    if(parsed.ec != std::errc() || parsed.ptr != end)
        return MakeUnexpected(Failure{});
    return value;
}

// Keeps the std::sto* rules: current C locale, leading space, sign, base prefix and a partial parse.
// The caller's errno is left unchanged.
template<typename NumberT, typename ParseT>
[[nodiscard]] inline Expected<NumericTextPrefix<NumberT>, ErrorCode> ParseNumberPrefix(const char* const begin, ParseT&& parse)noexcept([]()consteval noexcept{
    using ParsedT = decltype(DeclVal<ParseT&>()(DeclVal<const char* const&>(), DeclVal<char**>()));
    constexpr bool s_ResultIsNothrow =
        noexcept(DeclVal<ParseT&>()(DeclVal<const char* const&>(), DeclVal<char**>()))
        && IsNothrowDestructible_V<ParsedT>
        && noexcept(Expected<NumericTextPrefix<NumberT>, ErrorCode>(NumericTextPrefix<NumberT>{ .value = static_cast<NumberT>(DeclVal<const ParsedT&>()), .consumed = DeclVal<usize>() }))
    ;
    if constexpr(IsIntegral_V<NumberT> && sizeof(NumberT) < sizeof(ParsedT)){
        return s_ResultIsNothrow && noexcept(
            false || DeclVal<const ParsedT&>() < static_cast<ParsedT>(Limit<NumberT>::s_Min) || DeclVal<const ParsedT&>() > static_cast<ParsedT>(Limit<NumberT>::s_Max)
        );
    }
    else
        return s_ResultIsNothrow;
}()){
    using ParsedT = decltype(parse(begin, DeclVal<char**>()));

    const i32 previousErrno = errno;
    errno = 0;
    char* end = nullptr;
    const ParsedT parsed = parse(begin, &end);
    bool outOfRange = errno == ERANGE;
    errno = previousErrno;
    // An unsupported base can leave `end` unset.
    if(!end || end == begin)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    if constexpr(IsIntegral_V<NumberT> && sizeof(NumberT) < sizeof(ParsedT))
        outOfRange = outOfRange || parsed < static_cast<ParsedT>(Limit<NumberT>::s_Min) || parsed > static_cast<ParsedT>(Limit<NumberT>::s_Max);
    if(outOfRange)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::result_out_of_range));
    return NumericTextPrefix<NumberT>{ .value = static_cast<NumberT>(parsed), .consumed = static_cast<usize>(end - begin) };
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<i64> ParseI64FromChars(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<i64>(text);
}

// Stops at the first nondigit and wraps modulo 2^64, including overlong prefixes.
template<typename CharT>
[[nodiscard]] inline constexpr u64 ParseDecimalPrefixU64(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    u64 value = 0u;
    for(const CharT ch : text){
        if(ch < static_cast<CharT>('0') || ch > static_cast<CharT>('9'))
            break;
        value = value * 10u + static_cast<u64>(ch - static_cast<CharT>('0'));
    }
    return value;
}

[[nodiscard]] inline Expected<u64> ParseU64FromChars(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<u64>(text);
}

[[nodiscard]] inline Expected<f64> ParseF64FromChars(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<f64>(text);
}

[[nodiscard]] inline Expected<f32> ParseF32FromChars(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<f32>(text);
}

[[nodiscard]] inline Expected<u64> ParseU64(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<u64>(text);
}

[[nodiscard]] inline Expected<i64> ParseI64(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<i64>(text);
}


template<typename ArenaT>
[[nodiscard]] inline Expected<NumericTextPrefix<i32>, ErrorCode> Stoi(const AString<ArenaT>& str, const i32 base = TextDetail::s_DefaultNumericTextBase)noexcept{
    return TextParseDetail::ParseNumberPrefix<i32>(str.c_str(), [base](const char* const text, char** const end)noexcept{ return std::strtol(text, end, base); });
}

template<typename ArenaT>
[[nodiscard]] inline Expected<NumericTextPrefix<i64>, ErrorCode> Stoll(const AString<ArenaT>& str, const i32 base = TextDetail::s_DefaultNumericTextBase)noexcept{
    return TextParseDetail::ParseNumberPrefix<i64>(str.c_str(), [base](const char* const text, char** const end)noexcept{ return std::strtoll(text, end, base); });
}

template<typename ArenaT>
[[nodiscard]] inline Expected<NumericTextPrefix<u64>, ErrorCode> Stoull(const AString<ArenaT>& str, const i32 base = TextDetail::s_DefaultNumericTextBase)noexcept{
    return TextParseDetail::ParseNumberPrefix<u64>(str.c_str(), [base](const char* const text, char** const end)noexcept{ return std::strtoull(text, end, base); });
}

template<typename ArenaT>
[[nodiscard]] inline Expected<NumericTextPrefix<f32>, ErrorCode> Stof(const AString<ArenaT>& str)noexcept{
    return TextParseDetail::ParseNumberPrefix<f32>(str.c_str(), [](const char* const text, char** const end)noexcept{ return std::strtof(text, end); });
}

template<typename ArenaT>
[[nodiscard]] inline Expected<NumericTextPrefix<f64>, ErrorCode> Stod(const AString<ArenaT>& str)noexcept{
    return TextParseDetail::ParseNumberPrefix<f64>(str.c_str(), [](const char* const text, char** const end)noexcept{ return std::strtod(text, end); });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline Expected<u8> ParseHexDigit(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    if(ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9')){
        return static_cast<u8>(ch - static_cast<CharT>('0'));
    }
    if(ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f')){
        return static_cast<u8>(ch - static_cast<CharT>('a') + s_HexDecimalDigitCount);
    }
    if(ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F')){
        return static_cast<u8>(ch - static_cast<CharT>('A') + s_HexDecimalDigitCount);
    }
    return MakeUnexpected(Failure{});
}


template<typename CharT>
[[nodiscard]] inline Expected<u64> ParseHexU64(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    if(text.size() != s_HexU64DigitCount)
        return MakeUnexpected(Failure{});

    u64 value = 0u;
    for(const CharT ch : text){
        const auto nibble = ParseHexDigit(ch);
        if(!nibble)
            return MakeUnexpected(Failure{});

        value = (value << s_HexNibbleBits) | static_cast<u64>(*nibble);
    }

    return value;
}


template<typename CharT>
[[nodiscard]] inline Expected<u64> ParseVariableHexU64(BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    u64 value = 0u;
    if(text.size() >= s_HexPrefixLength && text[0u] == static_cast<CharT>('0') && (text[1u] == static_cast<CharT>('x') || text[1u] == static_cast<CharT>('X')))
        text.remove_prefix(s_HexPrefixLength);
    if(text.empty() || text.size() > s_HexU64DigitCount)
        return MakeUnexpected(Failure{});

    for(const CharT ch : text){
        const auto nibble = ParseHexDigit(ch);
        if(!nibble)
            return MakeUnexpected(Failure{});

        value = (value << s_HexNibbleBits) | static_cast<u64>(*nibble);
    }
    return value;
}


namespace NumericTextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline constexpr CharT HexDigit(const usize nibble)noexcept(IsArithmetic_V<CharT>){
    return static_cast<CharT>(nibble < s_HexDecimalDigitCount ? static_cast<usize>('0') + nibble : static_cast<usize>('a') + (nibble - s_HexDecimalDigitCount));
}

template<typename StringT>
inline void AppendFixedHexImpl(const u64 value, const u32 nibbleCount, StringT& outText)noexcept(
    noexcept(static_cast<bool>(outText.size() > outText.max_size() || outText.max_size() - outText.size() < nibbleCount))
    && noexcept(outText.push_back(HexDigit<typename StringT::value_type>(DeclVal<const usize&>())))
){
    if(outText.size() > outText.max_size() || outText.max_size() - outText.size() < nibbleCount)
        return;

    using CharT = typename StringT::value_type;
    for(u32 nibbleIndex = 0u; nibbleIndex < nibbleCount; ++nibbleIndex){
        const u32 shift = (nibbleCount - 1u - nibbleIndex) * s_HexNibbleBits;
        const usize nibble = static_cast<usize>((value >> shift) & s_HexNibbleMask);
        outText.push_back(HexDigit<CharT>(nibble));
    }
}

template<typename StringT>
inline void AppendHexU32Impl(const u32 value, StringT& outText)noexcept(noexcept(AppendFixedHexImpl(static_cast<u64>(value), s_HexU32DigitCount, outText))){
    AppendFixedHexImpl(static_cast<u64>(value), s_HexU32DigitCount, outText);
}

template<typename StringT>
inline void AppendHexU64Impl(const u64 value, StringT& outText)noexcept(noexcept(AppendFixedHexImpl(value, s_HexU64DigitCount, outText))){
    AppendFixedHexImpl(value, s_HexU64DigitCount, outText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename ArenaT>
inline void AppendHexU32(const u32 value, BasicString<CharT, ArenaT>& outText)noexcept(noexcept(NumericTextDetail::AppendHexU32Impl(value, outText))){
    NumericTextDetail::AppendHexU32Impl(value, outText);
}

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU32(const u32 value, StringT& outText)noexcept(noexcept(NumericTextDetail::AppendHexU32Impl(value, outText))){
    NumericTextDetail::AppendHexU32Impl(value, outText);
}

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text += typename StringT::value_type{};
    }
inline void AppendHexU32UnsignedLiteral(const u32 value, StringT& outText)noexcept(noexcept(outText += static_cast<typename StringT::value_type>('0')) && noexcept(AppendHexU32(value, outText))){
    using CharT = typename StringT::value_type;
    outText += static_cast<CharT>('0');
    outText += static_cast<CharT>('x');
    AppendHexU32(value, outText);
    outText += static_cast<CharT>('u');
}

template<typename CharT, typename ArenaT>
inline void AppendHexU64(const u64 value, BasicString<CharT, ArenaT>& outText)noexcept(noexcept(NumericTextDetail::AppendHexU64Impl(value, outText))){
    NumericTextDetail::AppendHexU64Impl(value, outText);
}

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU64(const u64 value, StringT& outText)noexcept(noexcept(NumericTextDetail::AppendHexU64Impl(value, outText))){
    NumericTextDetail::AppendHexU64Impl(value, outText);
}


template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> FormatHex64A(ArenaT& arena, const u64 value){
    AString<ArenaT> result{arena};
    result.reserve(s_HexU64DigitCount);
    AppendHexU64(value, result);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

