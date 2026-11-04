// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "basic_string.h"
#include "expected.h"
#include "error_code.h"
#include "limit.h"
#include "simplemath.h"
#include "span.h"

#include <charconv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr i32 s_DefaultNumericTextBase = 10;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DecimalTextBufferBytes = 32u;
inline constexpr usize s_UsizeDecimalDigitCount = []()noexcept{
    usize digits = 1u;
    for(usize value = Limit<usize>::s_Max; value >= 10u; value /= 10u)
        ++digits;
    return digits;
}();

inline constexpr usize s_HexDecimalDigitCount = 10u;
inline constexpr usize s_HexPrefixLength = 2u;
inline constexpr usize s_HexU32DigitCount = 8u;
inline constexpr usize s_HexU64DigitCount = 16u;
inline constexpr u32 s_HexNibbleBits = 4u;
inline constexpr u64 s_HexNibbleMask = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caller-owned bounded storage keeps scalar formatting independent of an allocator or locale.
[[nodiscard]] inline Expected<AStringView, ErrorCode> FormatI64(const i64 value, const Span<char> buffer)noexcept{
    char* const begin = buffer.data();
    const auto result = std::to_chars(begin, begin + buffer.size(), value);
    if(result.ec != ErrorCondition{})
        return MakeUnexpected(MakeErrorCode(result.ec));
    return AStringView(begin, static_cast<usize>(result.ptr - begin));
}

// The shortest general representation roundtrips to the same finite double, including signed zero.
[[nodiscard]] inline Expected<AStringView, ErrorCode> FormatF64(const f64 value, const Span<char> buffer)noexcept{
    if(!IsFinite(value))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    char* const begin = buffer.data();
    const auto result = std::to_chars(begin, begin + buffer.size(), value);
    if(result.ec != ErrorCondition{})
        return MakeUnexpected(MakeErrorCode(result.ec));
    return AStringView(begin, static_cast<usize>(result.ptr - begin));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Full-range caller storage makes decimal formatting total, including zero and the largest usize.
namespace TextDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename UInt>
    requires(IsIntegral_V<UInt> && !IsSigned_V<UInt> && !IsSame_V<UInt, b8>)
[[nodiscard]] inline constexpr AStringView FormatDecimalBuffer(const UInt value, char* const buffer)noexcept{
    UInt remaining = value;
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


};

[[nodiscard]] inline constexpr usize SkipNumericSign(const AStringView text)noexcept{
    return !text.empty() && (text[0u] == '+' || text[0u] == '-') ? 1u : 0u;
}

inline constexpr u8 s_NumericGrammarComplete = 0u;
inline constexpr u8 s_NumericGrammarIncomplete = 1u;
inline constexpr u8 s_NumericGrammarInvalid = 2u;

[[nodiscard]] inline u8 ClassifyIntegerGrammar(const AStringView text)noexcept{
    const usize begin = SkipNumericSign(text);
    if(begin == text.size())
        return s_NumericGrammarIncomplete;
    for(usize index = begin; index < text.size(); ++index){
        const u8 ch = static_cast<u8>(text[index]);
        if(ch < '0' || ch > '9')
            return s_NumericGrammarInvalid;
    }
    return s_NumericGrammarComplete;
}

[[nodiscard]] inline u8 ClassifyFloatGrammar(const AStringView text)noexcept{
    usize index = SkipNumericSign(text);
    b8 digits = false;
    while(index < text.size() && text[index] >= '0' && text[index] <= '9'){
        digits = true;
        ++index;
    }
    if(index < text.size() && text[index] == '.'){
        ++index;
        while(index < text.size() && text[index] >= '0' && text[index] <= '9'){
            digits = true;
            ++index;
        }
    }
    if(!digits){
        if(index == text.size())
            return s_NumericGrammarIncomplete;
        return s_NumericGrammarInvalid;
    }
    if(index < text.size() && (text[index] == 'e' || text[index] == 'E')){
        ++index;
        if(index < text.size() && (text[index] == '+' || text[index] == '-'))
            ++index;
        const usize exponent = index;
        while(index < text.size() && text[index] >= '0' && text[index] <= '9')
            ++index;
        if(exponent == index){
            if(index == text.size())
                return s_NumericGrammarIncomplete;
            return s_NumericGrammarInvalid;
        }
    }
    if(index == text.size())
        return s_NumericGrammarComplete;
    return s_NumericGrammarInvalid;
}


template<usize N>
    requires (N >= s_UsizeDecimalDigitCount)
[[nodiscard]] inline constexpr AStringView FormatDecimal(const usize value, char (&buffer)[N])noexcept{
    return TextDetail::FormatDecimalBuffer(value, buffer);
}

template<usize N>
    requires (N >= s_UsizeDecimalDigitCount)
[[nodiscard]] inline constexpr AStringView FormatDecimal(const usize value, Array<char, N>& buffer)noexcept{
    return TextDetail::FormatDecimalBuffer(value, buffer.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
inline void AppendUnsignedDecimal(StringT& out, const usize value)noexcept(noexcept(out.push_back(char{}))){
    Array<char, s_UsizeDecimalDigitCount> buffer;
    const AStringView digits = FormatDecimal(value, buffer);
    for(const char digit : digits)
        out.push_back(digit);
}


inline std::ios_base& StreamHex(std::ios_base& stream)noexcept(noexcept(stream.setf(std::ios_base::hex, std::ios_base::basefield))){ return std::hex(stream); }

inline std::ios_base& StreamDec(std::ios_base& stream)noexcept(noexcept(stream.setf(std::ios_base::dec, std::ios_base::basefield))){ return std::dec(stream); }


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
    if(parsed.ec != ErrorCondition{} || parsed.ptr != end)
        return MakeUnexpected(Failure{});
    return value;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] inline Expected<f64> ParseF64FromChars(const AStringView text)noexcept{
    return TextParseDetail::ParseNumber<f64>(text);
}

[[nodiscard]] inline Expected<f32> TryFiniteF32FromF64(const f64 value)noexcept{
    if(!IsFinite(value) || value < static_cast<f64>(Limit<f32>::s_Min) || value > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(value);
}

[[nodiscard]] inline Expected<f32> ParseFiniteF32ViaF64(const AStringView text)noexcept{
    const auto parsed = ParseF64FromChars(text);
    if(!parsed)
        return MakeUnexpected(Failure{});
    return TryFiniteF32FromF64(*parsed);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline Expected<u8> ParseHexDigit(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    if(ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        return static_cast<u8>(ch - static_cast<CharT>('0'));
    if(ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f'))
        return static_cast<u8>(ch - static_cast<CharT>('a') + s_HexDecimalDigitCount);
    if(ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F'))
        return static_cast<u8>(ch - static_cast<CharT>('A') + s_HexDecimalDigitCount);
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
    noexcept(static_cast<b8>(outText.size() > outText.max_size() || outText.max_size() - outText.size() < nibbleCount))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
inline void AppendHexByte(const u8 byte, StringT& outText)noexcept(noexcept(outText += char{})){
    outText += NumericTextDetail::HexDigit<char>(static_cast<usize>(byte >> s_HexNibbleBits));
    outText += NumericTextDetail::HexDigit<char>(static_cast<usize>(byte & s_HexNibbleMask));
}

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU32(const u32 value, StringT& outText)noexcept(noexcept(NumericTextDetail::AppendFixedHexImpl(static_cast<u64>(value), s_HexU32DigitCount, outText))){
    NumericTextDetail::AppendFixedHexImpl(static_cast<u64>(value), s_HexU32DigitCount, outText);
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

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU64(const u64 value, StringT& outText)noexcept(noexcept(NumericTextDetail::AppendFixedHexImpl(value, s_HexU64DigitCount, outText))){
    NumericTextDetail::AppendFixedHexImpl(value, s_HexU64DigitCount, outText);
}

template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> FormatHex64A(ArenaT& arena, const u64 value){
    AString<ArenaT> result{arena};
    result.reserve(s_HexU64DigitCount);
    AppendHexU64(value, result);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

