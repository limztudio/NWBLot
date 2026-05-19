// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_utils.h"
#include "compile.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 FNV64_OFFSET_BASIS = 14695981039346656037ull;
inline constexpr u64 FNV64_PRIME = 1099511628211ull;


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
        outValue = static_cast<u8>(ch - static_cast<CharT>('a') + 10);
        return true;
    }
    if(ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F')){
        outValue = static_cast<u8>(ch - static_cast<CharT>('A') + 10);
        return true;
    }
    return false;
}


template<typename CharT>
[[nodiscard]] inline bool ParseHexU64(const BasicStringView<CharT> text, u64& outValue){
    if(text.size() != 16)
        return false;

    outValue = 0;
    for(const CharT ch : text){
        u8 nibble = 0;
        if(!ParseHexDigit(ch, nibble))
            return false;

        outValue = (outValue << 4) | static_cast<u64>(nibble);
    }

    return true;
}


template<typename CharT, typename ArenaT>
inline void AppendHexU64(const u64 value, BasicString<CharT, ArenaT>& outText){
    static constexpr CharT s_HexDigits[16] = {
        static_cast<CharT>('0'), static_cast<CharT>('1'), static_cast<CharT>('2'), static_cast<CharT>('3'),
        static_cast<CharT>('4'), static_cast<CharT>('5'), static_cast<CharT>('6'), static_cast<CharT>('7'),
        static_cast<CharT>('8'), static_cast<CharT>('9'), static_cast<CharT>('a'), static_cast<CharT>('b'),
        static_cast<CharT>('c'), static_cast<CharT>('d'), static_cast<CharT>('e'), static_cast<CharT>('f')
    };
    if(outText.size() > outText.max_size() - 16u)
        return;

    for(u32 nibbleIndex = 0; nibbleIndex < 16u; ++nibbleIndex){
        const u32 shift = (15 - nibbleIndex) * 4;
        const usize nibble = static_cast<usize>((value >> shift) & 0xF);
        outText.push_back(s_HexDigits[nibble]);
    }
}
template<typename StringT>
    requires requires(StringT& text){
        typename StringT::value_type;
        text.size();
        text.max_size();
        text.push_back(typename StringT::value_type{});
    }
inline void AppendHexU64(const u64 value, StringT& outText){
    using CharT = typename StringT::value_type;
    static constexpr CharT s_HexDigits[16] = {
        static_cast<CharT>('0'), static_cast<CharT>('1'), static_cast<CharT>('2'), static_cast<CharT>('3'),
        static_cast<CharT>('4'), static_cast<CharT>('5'), static_cast<CharT>('6'), static_cast<CharT>('7'),
        static_cast<CharT>('8'), static_cast<CharT>('9'), static_cast<CharT>('a'), static_cast<CharT>('b'),
        static_cast<CharT>('c'), static_cast<CharT>('d'), static_cast<CharT>('e'), static_cast<CharT>('f')
    };
    if(outText.size() > outText.max_size() - 16u)
        return;

    for(u32 nibbleIndex = 0; nibbleIndex < 16u; ++nibbleIndex){
        const u32 shift = (15 - nibbleIndex) * 4;
        const usize nibble = static_cast<usize>((value >> shift) & 0xF);
        outText.push_back(s_HexDigits[nibble]);
    }
}


template<typename ArenaT>
[[nodiscard]] inline AString<ArenaT> FormatHex64A(ArenaT& arena, const u64 value){
    AString<ArenaT> result{arena};
    result.reserve(16);
    AppendHexU64(value, result);
    return result;
}
template<typename ArenaT>
[[nodiscard]] inline WString<ArenaT> FormatHex64W(ArenaT& arena, const u64 value){
    WString<ArenaT> result{arena};
    result.reserve(16);
    AppendHexU64(value, result);
    return result;
}
template<typename ArenaT>
[[nodiscard]] inline auto FormatHex64(ArenaT& arena, const u64 value){ return FormatHex64A(arena, value); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

