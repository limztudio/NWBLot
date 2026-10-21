// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "expected.h"
#include "hash_utils.h"
#include "numeric_text.h"
#include "not_null.h"

#include <functional>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Name;

inline constexpr u32 s_NameHashLaneCount = 8u;

struct NameHash{
    u64 qwords[s_NameHashLaneCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameTextError{ enum Enum : u8{ NullText, EmbeddedNull }; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_HashLaneCount = s_NameHashLaneCount;
inline constexpr usize s_DebugNameCapacity = 256u;
inline constexpr usize s_NameHashBytes = sizeof(u64) * s_HashLaneCount;
inline constexpr usize s_HexDigitsPerHashLane = sizeof(u64) * 2u;
inline constexpr usize s_EncodedNameHashLength = s_HexDigitsPerHashLane * s_HashLaneCount;
inline constexpr usize s_DebugHashTextLength = s_EncodedNameHashLength + (s_HashLaneCount - 1u);
inline constexpr i32 s_HashHexInitialBitShift = static_cast<i32>((s_HexU64DigitCount - 1u) * s_HexNibbleBits);
inline constexpr i32 s_HashHexBitShiftStep = static_cast<i32>(s_HexNibbleBits);
inline constexpr u32 s_NameHashByteBitCount = 8u;
inline constexpr u64 s_NameHashByteMask = 0xFFu;

static_assert(sizeof(NameHash) == s_NameHashBytes, "NameHash size must stay stable");

inline constexpr u64 s_LaneSeeds[s_HashLaneCount] = {
    s_Fnv64OffsetBasis,
    s_Fnv64OffsetBasis ^ 0x0123456789ABCDEFull,
    s_Fnv64OffsetBasis ^ 0xFEDCBA9876543210ull,
    s_Fnv64OffsetBasis ^ 0x0F1E2D3C4B5A6978ull,
    s_Fnv64OffsetBasis ^ 0x8796A5B4C3D2E1F0ull,
    s_Fnv64OffsetBasis ^ 0xDEADBEEFCAFEBABEull,
    s_Fnv64OffsetBasis ^ 0x1234ABCD5678EF01ull,
    s_Fnv64OffsetBasis ^ 0xA0B1C2D3E4F50617ull,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(s_LaneSeeds) / sizeof(s_LaneSeeds[0]) == s_HashLaneCount, "s_LaneSeeds count must match s_HashLaneCount");


inline constexpr void InitializeNameHash(NameHash& hash)noexcept{
    for(u32 i = 0u; i < s_HashLaneCount; ++i)
        hash.qwords[i] = s_LaneSeeds[i];
}

template<typename CharT>
inline constexpr void UpdateCanonicalNameHashLanes(NameHash& hash, const CharT ch)noexcept(IsArithmetic_V<CharT>){
    const u64 byte = static_cast<u64>(static_cast<u8>(Canonicalize(ch)));
    for(u32 i = 0u; i < s_HashLaneCount; ++i){
        hash.qwords[i] ^= byte;
        hash.qwords[i] *= s_Fnv64Prime;
    }
}

template<typename CharT>
[[nodiscard]] inline constexpr bool UpdateCanonicalNameHashText(NameHash& hash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT{})
            return false;
        UpdateCanonicalNameHashLanes(hash, ch);
    }

    return true;
}

template<typename CharT>
[[nodiscard]] inline bool IsNameHashTokenChar(const CharT ch)noexcept(IsArithmetic_V<CharT>){
    return (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9')) || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f')) || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F')) || ch == static_cast<CharT>('_');
}

template<typename CharT>
[[nodiscard]] inline Expected<Array<char, s_DebugHashTextLength + 1u>> CopyDebugHashToken(const BasicStringView<CharT> text, const usize offset)noexcept(IsArithmetic_V<CharT>){
    Array<char, s_DebugHashTextLength + 1u> hashText;
    if(offset > text.size() || s_DebugHashTextLength > text.size() - offset)
        return MakeUnexpected(Failure{});
    if(offset > 0u && IsNameHashTokenChar(text[offset - 1u]))
        return MakeUnexpected(Failure{});
    if(offset + s_DebugHashTextLength < text.size() && IsNameHashTokenChar(text[offset + s_DebugHashTextLength]))
        return MakeUnexpected(Failure{});

    for(usize i = 0u; i < s_DebugHashTextLength; ++i){
        const CharT ch = text[offset + i];
        if(((i + 1u) % (s_HexDigitsPerHashLane + 1u)) == 0u){
            if(ch != static_cast<CharT>('_'))
                return MakeUnexpected(Failure{});
        }
        else if(!IsNameHashTokenChar(ch) || ch == static_cast<CharT>('_')){
            return MakeUnexpected(Failure{});
        }

        hashText[i] = static_cast<char>(ch);
    }
    hashText[s_DebugHashTextLength] = 0;
    return hashText;
}

[[nodiscard]] inline Expected<NameHash> DecodeDebugHashText(const AStringView text)noexcept{
    if(text.size() != s_DebugHashTextLength)
        return MakeUnexpected(Failure{});

    NameHash hash;
    usize cursor = 0u;
    for(u32 lane = 0u; lane < s_HashLaneCount; ++lane){
        if((lane > 0u && text[cursor++] != '_') || cursor + s_HexDigitsPerHashLane > text.size())
            return MakeUnexpected(Failure{});

        const auto laneHash = ParseHexU64<char>(AStringView(text.data() + cursor, s_HexDigitsPerHashLane));
        if(!laneHash)
            return MakeUnexpected(Failure{});
        hash.qwords[lane] = *laneHash;
        cursor += s_HexDigitsPerHashLane;
    }

    if(cursor != text.size())
        return MakeUnexpected(Failure{});
    return hash;
}

inline constexpr bool EqualHash(const NameHash& a, const NameHash& b)noexcept{
    for(u32 i = 0u; i < s_HashLaneCount; ++i){
        if(a.qwords[i] != b.qwords[i])
            return false;
    }
    return true;
}

inline constexpr bool IsZeroHash(const NameHash& hash)noexcept{
    for(u32 i = 0u; i < s_HashLaneCount; ++i){
        if(hash.qwords[i] != 0)
            return false;
    }
    return true;
}

inline constexpr usize HashValue(const NameHash& hash)noexcept{
    usize seed = static_cast<usize>(hash.qwords[0]);
    for(u32 i = 1; i < s_HashLaneCount; ++i){
        HashCombineHash(seed, static_cast<usize>(hash.qwords[i]), static_cast<usize>(s_HashCombineGoldenRatio64));
    }
    return seed;
}

template<typename CharT>
inline void HashToDebugString(const NameHash& hash, CharT* dst, const usize dstSize)noexcept(IsArithmetic_V<CharT>){
    if(dstSize == 0)
        return;

    static constexpr AStringView s_Hex = "0123456789abcdef";

    if(dstSize <= s_DebugHashTextLength){
        dst[0] = CharT{};
        return;
    }

    CharT* writeCursor = dst;
    for(u32 i = 0u; i < s_HashLaneCount; ++i){
        if(i > 0){
            *writeCursor = static_cast<CharT>('_');
            ++writeCursor;
        }
        const u64 value = hash.qwords[i];
        for(i32 bitShift = s_HashHexInitialBitShift; bitShift >= 0; bitShift -= s_HashHexBitShiftStep){
            *writeCursor = static_cast<CharT>(s_Hex[(value >> bitShift) & s_HexNibbleMask]);
            ++writeCursor;
        }
    }
    *writeCursor = CharT{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline constexpr NameHash ComputeNameHash(const NotNull<const CharT*> text)noexcept(IsArithmetic_V<CharT>){
    NameHash hash;
    NameDetail::InitializeNameHash(hash);
    for(usize index = 0u; text.get()[index] != CharT{}; ++index)
        NameDetail::UpdateCanonicalNameHashLanes(hash, text.get()[index]);
    return hash;
}

template<typename CharT>
[[nodiscard]] inline constexpr Expected<NameHash, NameTextError::Enum> ComputeNameHash(const CharT* text)noexcept(IsArithmetic_V<CharT>){
    if(text == nullptr)
        return MakeUnexpected(NameTextError::NullText);
    return ComputeNameHash(MakeNotNull(text));
}

template<typename CharT>
[[nodiscard]] inline constexpr Expected<NameHash, NameTextError::Enum> ComputeNameHash(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    NameHash hash;
    NameDetail::InitializeNameHash(hash);
    if(!NameDetail::UpdateCanonicalNameHashText(hash, text))
        return MakeUnexpected(NameTextError::EmbeddedNull);
    return hash;
}


template<typename CharT>
class NameText{
    friend class Name;
    template<typename T>
    friend constexpr NameHash ComputeNameHash(NameText<T> text)noexcept(IsArithmetic_V<T>);


public:
    // Only constant evaluation can admit a bounded text value to the infallible constructor path.
    consteval NameText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>)
        : m_text(text)
    {
        for(const CharT character : text){
            if(character == CharT{})
                __builtin_trap();
        }
    }
    template<usize TextSize>
    consteval NameText(const CharT (&text)[TextSize])noexcept(IsArithmetic_V<CharT>)
        : NameText(BasicStringView<CharT>(text, TextSize - 1u))
    {
        if(text[TextSize - 1u] != CharT{})
            __builtin_trap();
    }


public:
    [[nodiscard]] constexpr BasicStringView<CharT> text()const noexcept{ return m_text; }


private:
    BasicStringView<CharT> m_text;
};


template<typename CharT>
[[nodiscard]] inline constexpr NameHash ComputeNameHash(const NameText<CharT> text)noexcept(IsArithmetic_V<CharT>){
    NameHash hash;
    NameDetail::InitializeNameHash(hash);
    for(const CharT character : text.m_text)
        NameDetail::UpdateCanonicalNameHashLanes(hash, character);
    return hash;
}


[[nodiscard]] inline constexpr bool LessNameHash(const NameHash& lhs, const NameHash& rhs)noexcept{
    for(u32 i = 0u; i < NameDetail::s_HashLaneCount; ++i){
        if(lhs.qwords[i] < rhs.qwords[i])
            return true;
        if(lhs.qwords[i] > rhs.qwords[i])
            return false;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr bool operator==(const NameHash& a, const NameHash& b)noexcept{
    return NameDetail::EqualHash(a, b);
}
inline constexpr bool operator!=(const NameHash& a, const NameHash& b)noexcept{
    return !(a == b);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<NameHash>{
    usize operator()(const NameHash& h)const noexcept{
        return NameDetail::HashValue(h);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

