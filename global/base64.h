// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Base64Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
inline constexpr u8 s_InvalidDigit = 255u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr u8 Digit(const char character){
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


// Canonical RFC 4648 alphabet and padding only. Failure preserves outSize; no allocation occurs here.
[[nodiscard]] inline bool Base64DecodedSize(const AStringView source, const usize maxBytes, usize& outSize){
    if(source.size() % 4u != 0u)
        return false;
    if(source.empty()){
        outSize = 0u;
        return true;
    }
    const usize padding = source.back() == '=' ? (source[source.size() - 2u] == '=' ? 2u : 1u) : 0u;
    const usize byteCount = source.size() / 4u * 3u - padding;
    if(byteCount > maxBytes)
        return false;
    for(usize offset = 0u; offset < source.size(); offset += 4u){
        const u8 first = Base64Detail::Digit(source[offset]);
        const u8 second = Base64Detail::Digit(source[offset + 1u]);
        if(first == Base64Detail::s_InvalidDigit || second == Base64Detail::s_InvalidDigit)
            return false;
        const bool last = offset + 4u == source.size();
        if(source[offset + 2u] == '='){
            if(!last || source[offset + 3u] != '=' || (second & 15u) != 0u)
                return false;
            continue;
        }
        const u8 third = Base64Detail::Digit(source[offset + 2u]);
        if(third == Base64Detail::s_InvalidDigit)
            return false;
        if(source[offset + 3u] == '='){
            if(!last || (third & 3u) != 0u)
                return false;
        }
        else if(Base64Detail::Digit(source[offset + 3u]) == Base64Detail::s_InvalidDigit)
            return false;
    }
    outSize = byteCount;
    return true;
}

// Candidate ownership makes successful replacement and aliased inputs safe. Failure preserves outBytes.
template<typename ByteContainer>
[[nodiscard]] inline bool DecodeBase64(const AStringView source, ByteContainer& outBytes, const usize maxBytes){
    BinaryDetail::RequireByteContainer<ByteContainer>();
    usize byteCount = 0u;
    if(!Base64DecodedSize(source, maxBytes, byteCount) || byteCount > outBytes.max_size())
        return false;
    ByteContainer candidate(outBytes.get_allocator());
    candidate.resize(byteCount);
    usize target = 0u;
    for(usize offset = 0u; offset < source.size(); offset += 4u){
        const u32 first = Base64Detail::Digit(source[offset]);
        const u32 second = Base64Detail::Digit(source[offset + 1u]);
        const u32 third = source[offset + 2u] == '=' ? 0u : Base64Detail::Digit(source[offset + 2u]);
        const u32 fourth = source[offset + 3u] == '=' ? 0u : Base64Detail::Digit(source[offset + 3u]);
        const u32 packed = (first << 18u) | (second << 12u) | (third << 6u) | fourth;
        candidate[target] = static_cast<typename ByteContainer::value_type>(packed >> 16u);
        ++target;
        if(target < byteCount){
            candidate[target] = static_cast<typename ByteContainer::value_type>(packed >> 8u);
            ++target;
        }
        if(target < byteCount){
            candidate[target] = static_cast<typename ByteContainer::value_type>(packed);
            ++target;
        }
    }
    outBytes = Move(candidate);
    return true;
}

template<typename StringType>
[[nodiscard]] inline bool EncodeBase64(const BinaryByteView source, StringType& outText){
    static_assert(sizeof(typename StringType::value_type) == 1u, "base64 output requires a byte-sized string");
    if(!source.data() && !source.empty())
        return false;
    const usize quartets = source.size() / 3u + (source.size() % 3u != 0u ? 1u : 0u);
    if(quartets > Limit<usize>::s_Max / 4u || quartets * 4u > outText.max_size())
        return false;
    StringType candidate(outText.get_allocator());
    candidate.resize(quartets * 4u);
    usize target = 0u;
    for(usize offset = 0u; offset < source.size();){
        const usize remaining = source.size() - offset;
        const u32 first = source[offset];
        const u32 second = remaining > 1u ? source[offset + 1u] : 0u;
        const u32 third = remaining > 2u ? source[offset + 2u] : 0u;
        const u32 packed = (first << 16u) | (second << 8u) | third;
        candidate[target] = Base64Detail::s_Alphabet[(packed >> 18u) & 63u];
        candidate[target + 1u] = Base64Detail::s_Alphabet[(packed >> 12u) & 63u];
        candidate[target + 2u] = remaining > 1u ? Base64Detail::s_Alphabet[(packed >> 6u) & 63u] : '=';
        candidate[target + 3u] = remaining > 2u ? Base64Detail::s_Alphabet[packed & 63u] : '=';
        target += 4u;
        offset += remaining >= 3u ? 3u : remaining;
    }
    outText = Move(candidate);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

