#pragma once


#include "compile.h"
#include "text_utils.h"
#include "type.h"
#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 FNV64_OFFSET_BASIS = 14695981039346656037ull;
inline constexpr u64 FNV64_PRIME = 1099511628211ull;
inline constexpr usize s_HashCombineGoldenRatio = 0x9e3779b9u;
inline constexpr usize s_HashCombineLeftShift = 6u;
inline constexpr usize s_HashCombineRightShift = 2u;
inline constexpr usize s_HexDecimalDigitCount = 10u;
inline constexpr usize s_HexPrefixLength = 2u;
inline constexpr usize s_HexU32DigitCount = 8u;
inline constexpr usize s_HexU64DigitCount = 16u;
inline constexpr u32 s_HexNibbleBits = 4u;
inline constexpr u64 s_HexNibbleMask = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr u64 FNV1a64(const CharT* str, u64 seed){
    u64 hash = seed;
    if(str == nullptr)
        return hash;
    for(; *str != CharT{}; ++str){
        hash ^= static_cast<u64>(static_cast<u8>(Canonicalize(*str)));
        hash *= FNV64_PRIME;
    }
    return hash;
}

[[nodiscard]] inline u64 UpdateFnv64(u64 hash, const u8* bytes, const usize byteCount){
    if(bytes == nullptr || byteCount == 0)
        return hash;

    for(usize i = 0; i < byteCount; ++i){
        hash ^= static_cast<u64>(bytes[i]);
        hash *= FNV64_PRIME;
    }

    return hash;
}

template<typename T>
inline void Fnv64AppendValue(u64& inOutHash, const T& value){
    static_assert(IsTriviallyCopyable_V<T>, "Fnv64AppendValue requires a trivially copyable value");
    inOutHash = UpdateFnv64(inOutHash, reinterpret_cast<const u8*>(&value), sizeof(value));
}

inline void Fnv64AppendBuffer(u64& inOutHash, const u8* bytes, const usize byteCount){
    Fnv64AppendValue(inOutHash, byteCount);
    inOutHash = UpdateFnv64(inOutHash, bytes, byteCount);
}

inline void Fnv64AppendBool(u64& inOutHash, const bool value){
    const u8 byteValue = value ? 1u : 0u;
    Fnv64AppendValue(inOutHash, byteValue);
}

template<typename CharT>
[[nodiscard]] inline constexpr u64 UpdateFnv64TextCanonical(u64 hash, const BasicStringView<CharT> text){
    for(const CharT ch : text){
        hash ^= static_cast<u64>(static_cast<u8>(Canonicalize(ch)));
        hash *= FNV64_PRIME;
    }

    return hash;
}

template<typename CharT>
[[nodiscard]] inline u64 UpdateFnv64TextExact(u64 hash, const BasicStringView<CharT> text){
    return UpdateFnv64(
        hash,
        reinterpret_cast<const u8*>(text.data()),
        text.size() * sizeof(CharT)
    );
}

[[nodiscard]] inline u64 ComputeFnv64Bytes(const void* data, const usize byteCount){
    return UpdateFnv64(
        FNV64_OFFSET_BASIS,
        static_cast<const u8*>(data),
        byteCount
    );
}

inline void HashCombineHash(usize& seed, const usize hash){
    seed ^= hash
        + s_HashCombineGoldenRatio
        + (seed << s_HashCombineLeftShift)
        + (seed >> s_HashCombineRightShift)
    ;
}

template<typename T>
inline void HashCombine(usize& seed, const T& value){
    HashCombineHash(seed, Hasher<T>{}(value));
}

[[nodiscard]] inline u32 FloatHashBits(f32 value)noexcept{
    if(value == 0.0f)
        value = 0.0f;

    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

inline void HashCombineFloat(usize& seed, const f32 value)noexcept{
    HashCombine(seed, FloatHashBits(value));
}


template<typename CharT>
[[nodiscard]] inline u64 ComputeFnv64Text(const BasicStringView<CharT> text){
    return ComputeFnv64Bytes(text.data(), text.size() * sizeof(CharT));
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline u64 ComputeFnv64Text(const BasicString<CharT, ArenaT>& text){
    return ComputeFnv64Text(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline u64 ComputeFnv64Text(const StringT& text){
    using CharT = typename StringT::value_type;
    return ComputeFnv64Text(BasicStringView<CharT>(text.data(), text.size()));
}


template<typename CharT>
[[nodiscard]] inline bool ParseHexDigit(const CharT ch, u8& outValue){
    if(ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9')){
        outValue = static_cast<u8>(ch - static_cast<CharT>('0'));
        return true;
    }
    if(ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f')){
        outValue = static_cast<u8>(ch - static_cast<CharT>('a') + s_HexDecimalDigitCount);
        return true;
    }
    if(ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F')){
        outValue = static_cast<u8>(ch - static_cast<CharT>('A') + s_HexDecimalDigitCount);
        return true;
    }
    return false;
}


template<typename CharT>
[[nodiscard]] inline bool ParseHexU64(const BasicStringView<CharT> text, u64& outValue){
    if(text.size() != s_HexU64DigitCount)
        return false;

    outValue = 0;
    for(const CharT ch : text){
        u8 nibble = 0;
        if(!ParseHexDigit(ch, nibble))
            return false;

        outValue = (outValue << s_HexNibbleBits) | static_cast<u64>(nibble);
    }

    return true;
}


template<typename CharT>
[[nodiscard]] inline bool ParseVariableHexU64(BasicStringView<CharT> text, u64& outValue){
    outValue = 0u;
    if(text.size() >= s_HexPrefixLength && text[0u] == static_cast<CharT>('0') && (text[1u] == static_cast<CharT>('x') || text[1u] == static_cast<CharT>('X')))
        text.remove_prefix(s_HexPrefixLength);
    if(text.empty() || text.size() > s_HexU64DigitCount)
        return false;

    for(const CharT ch : text){
        u8 nibble = 0u;
        if(!ParseHexDigit(ch, nibble))
            return false;

        outValue = (outValue << s_HexNibbleBits) | static_cast<u64>(nibble);
    }
    return true;
}


namespace HashUtilsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline constexpr CharT HexDigit(const usize nibble){
    return static_cast<CharT>(
        nibble < s_HexDecimalDigitCount
            ? static_cast<usize>('0') + nibble
            : static_cast<usize>('a') + (nibble - s_HexDecimalDigitCount)
    );
}

template<typename StringT>
inline void AppendFixedHexImpl(const u64 value, const u32 nibbleCount, StringT& outText){
    if(outText.size() > outText.max_size() || outText.max_size() - outText.size() < nibbleCount)
        return;

    using CharT = typename StringT::value_type;
    for(u32 nibbleIndex = 0; nibbleIndex < nibbleCount; ++nibbleIndex){
        const u32 shift = (nibbleCount - 1u - nibbleIndex) * s_HexNibbleBits;
        const usize nibble = static_cast<usize>((value >> shift) & s_HexNibbleMask);
        outText.push_back(HexDigit<CharT>(nibble));
    }
}

template<typename StringT>
inline void AppendHexU32Impl(const u32 value, StringT& outText){
    AppendFixedHexImpl(static_cast<u64>(value), s_HexU32DigitCount, outText);
}

template<typename StringT>
inline void AppendHexU64Impl(const u64 value, StringT& outText){
    AppendFixedHexImpl(value, s_HexU64DigitCount, outText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename ArenaT>
inline void AppendHexU32(const u32 value, BasicString<CharT, ArenaT>& outText){
    HashUtilsDetail::AppendHexU32Impl(value, outText);
}
template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU32(const u32 value, StringT& outText){
    HashUtilsDetail::AppendHexU32Impl(value, outText);
}

template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text += typename StringT::value_type{};
    }
inline void AppendHexU32UnsignedLiteral(const u32 value, StringT& outText){
    using CharT = typename StringT::value_type;
    outText += static_cast<CharT>('0');
    outText += static_cast<CharT>('x');
    AppendHexU32(value, outText);
    outText += static_cast<CharT>('u');
}

template<typename CharT, typename ArenaT>
inline void AppendHexU64(const u64 value, BasicString<CharT, ArenaT>& outText){
    HashUtilsDetail::AppendHexU64Impl(value, outText);
}
template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU64(const u64 value, StringT& outText){
    HashUtilsDetail::AppendHexU64Impl(value, outText);
}


template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> FormatHex64A(ArenaT& arena, const u64 value){
    AString<ArenaT> result{arena};
    result.reserve(s_HexU64DigitCount);
    AppendHexU64(value, result);
    return result;
}
template<typename ArenaT>
[[nodiscard]] inline WString<ArenaT> FormatHex64W(ArenaT& arena, const u64 value){
    WString<ArenaT> result{arena};
    result.reserve(s_HexU64DigitCount);
    AppendHexU64(value, result);
    return result;
}
template<typename ArenaT>
[[nodiscard]] inline auto FormatHex64(ArenaT& arena, const u64 value){ return FormatHex64A(arena, value); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

