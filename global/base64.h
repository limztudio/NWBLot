// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Base64Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr u8 s_InvalidDigit = 255u;
inline constexpr usize s_QuartetCharCount = 4u;
inline constexpr usize s_QuartetByteCount = 3u;
inline constexpr usize s_MaxPaddingCount = 2u;
inline constexpr u32 s_FirstDigitShift = 18u;
inline constexpr u32 s_SecondDigitShift = 12u;
inline constexpr u32 s_ThirdDigitShift = 6u;
inline constexpr u32 s_FirstByteShift = 16u;
inline constexpr u32 s_SecondByteShift = 8u;
inline constexpr u32 s_DigitMask = 63u;
inline constexpr u32 s_DoublePaddingUnusedMask = 15u;
inline constexpr u32 s_SinglePaddingUnusedMask = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr u8 Digit(const char character)noexcept{
    if(character >= 'A' && character <= 'Z')
        return static_cast<u8>(character - 'A');
    if(character >= 'a' && character <= 'z')
        return static_cast<u8>(character - 'a' + 26);
    if(character >= '0' && character <= '9')
        return static_cast<u8>(character - '0' + 52);
    if(character == '+')
        return 62u;
    if(character == '/')
        return 63u;
    return s_InvalidDigit;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Canonical RFC 4648 alphabet and padding only; size admission never allocates.
[[nodiscard]] inline Expected<usize> Base64DecodedSize(const AStringView source, const usize maxBytes)noexcept{
    if(source.size() % Base64Detail::s_QuartetCharCount != 0u)
        return MakeUnexpected(Failure{});
    if(source.empty())
        return 0u;
    const usize padding = source.back() == '=' ? (source[source.size() - 2u] == '=' ? Base64Detail::s_MaxPaddingCount : 1u) : 0u;
    const usize byteCount = source.size() / Base64Detail::s_QuartetCharCount * Base64Detail::s_QuartetByteCount - padding;
    if(byteCount > maxBytes)
        return MakeUnexpected(Failure{});
    for(usize offset = 0u; offset < source.size(); offset += Base64Detail::s_QuartetCharCount){
        const u8 first = Base64Detail::Digit(source[offset]);
        const u8 second = Base64Detail::Digit(source[offset + 1u]);
        if(first == Base64Detail::s_InvalidDigit || second == Base64Detail::s_InvalidDigit)
            return MakeUnexpected(Failure{});
        const bool last = offset + Base64Detail::s_QuartetCharCount == source.size();
        if(source[offset + 2u] == '='){
            if(!last || source[offset + 3u] != '=' || (second & Base64Detail::s_DoublePaddingUnusedMask) != 0u)
                return MakeUnexpected(Failure{});
            continue;
        }
        const u8 third = Base64Detail::Digit(source[offset + 2u]);
        if(third == Base64Detail::s_InvalidDigit)
            return MakeUnexpected(Failure{});
        if(source[offset + 3u] == '='){
            if(!last || (third & Base64Detail::s_SinglePaddingUnusedMask) != 0u)
                return MakeUnexpected(Failure{});
        }
        else if(Base64Detail::Digit(source[offset + 3u]) == Base64Detail::s_InvalidDigit)
            return MakeUnexpected(Failure{});
    }
    return byteCount;
}

// The returned candidate owns its bytes; borrowed input may alias a caller-owned destination.
template<typename ByteContainer>
[[nodiscard]] inline Expected<ByteContainer> DecodeBase64(const AStringView source, const typename ByteContainer::allocator_type& allocator, const usize maxBytes){
    BinaryDetail::RequireByteContainer<ByteContainer>();
    const auto parsedSize = Base64DecodedSize(source, maxBytes);
    if(!parsedSize)
        return MakeUnexpected(parsedSize.error());
    ByteContainer candidate(allocator);
    const usize byteCount = *parsedSize;
    if(byteCount > candidate.max_size())
        return MakeUnexpected(Failure{});
    candidate.resize(byteCount);
    usize target = 0u;
    for(usize offset = 0u; offset < source.size(); offset += Base64Detail::s_QuartetCharCount){
        const u32 first = Base64Detail::Digit(source[offset]);
        const u32 second = Base64Detail::Digit(source[offset + 1u]);
        const u32 third = source[offset + 2u] == '=' ? 0u : Base64Detail::Digit(source[offset + 2u]);
        const u32 fourth = source[offset + 3u] == '=' ? 0u : Base64Detail::Digit(source[offset + 3u]);
        const u32 packed = (first << Base64Detail::s_FirstDigitShift) | (second << Base64Detail::s_SecondDigitShift) | (third << Base64Detail::s_ThirdDigitShift) | fourth;
        candidate[target] = static_cast<typename ByteContainer::value_type>(packed >> Base64Detail::s_FirstByteShift);
        ++target;
        if(target < byteCount){
            candidate[target] = static_cast<typename ByteContainer::value_type>(packed >> Base64Detail::s_SecondByteShift);
            ++target;
        }
        if(target < byteCount){
            candidate[target] = static_cast<typename ByteContainer::value_type>(packed);
            ++target;
        }
    }
    return candidate;
}

template<typename StringType>
[[nodiscard]] inline Expected<StringType> EncodeBase64(const BinaryByteView source, const typename StringType::allocator_type& allocator){
    static_assert(sizeof(typename StringType::value_type) == 1u, "base64 output requires a byte-sized string");
    if(!source.data() && !source.empty())
        return MakeUnexpected(Failure{});
    const usize quartets = source.size() / Base64Detail::s_QuartetByteCount + (source.size() % Base64Detail::s_QuartetByteCount != 0u ? 1u : 0u);
    if(quartets > Limit<usize>::s_Max / Base64Detail::s_QuartetCharCount)
        return MakeUnexpected(Failure{});
    StringType candidate(allocator);
    if(quartets * Base64Detail::s_QuartetCharCount > candidate.max_size())
        return MakeUnexpected(Failure{});
    candidate.resize(quartets * Base64Detail::s_QuartetCharCount);
    usize target = 0u;
    for(usize offset = 0u; offset < source.size();){
        const usize remaining = source.size() - offset;
        const u32 first = source[offset];
        const u32 second = remaining > 1u ? source[offset + 1u] : 0u;
        const u32 third = remaining > 2u ? source[offset + 2u] : 0u;
        const u32 packed = (first << Base64Detail::s_FirstByteShift) | (second << Base64Detail::s_SecondByteShift) | third;
        candidate[target] = Base64Detail::s_Alphabet[(packed >> Base64Detail::s_FirstDigitShift) & Base64Detail::s_DigitMask];
        candidate[target + 1u] = Base64Detail::s_Alphabet[(packed >> Base64Detail::s_SecondDigitShift) & Base64Detail::s_DigitMask];
        candidate[target + 2u] = remaining > 1u ? Base64Detail::s_Alphabet[(packed >> Base64Detail::s_ThirdDigitShift) & Base64Detail::s_DigitMask] : '=';
        candidate[target + 3u] = remaining > 2u ? Base64Detail::s_Alphabet[packed & Base64Detail::s_DigitMask] : '=';
        target += Base64Detail::s_QuartetCharCount;
        offset += remaining >= Base64Detail::s_QuartetByteCount ? Base64Detail::s_QuartetByteCount : remaining;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

