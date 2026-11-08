// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "expected.h"
#include "hash_utils.h"
#include "not_null.h"

#include <functional>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Name;

inline constexpr u32 s_NameHashLaneCount = 8u;

struct NameHash{
    Array<u64, s_NameHashLaneCount> qwords;
};

[[nodiscard]] inline constexpr u64 FoldNameHash64(const NameHash& nameHash)noexcept{
    u64 folded = nameHash.qwords[0u];
    for(usize lane = 1u; lane < nameHash.qwords.size(); ++lane)
        folded ^= nameHash.qwords[lane] + s_HashCombineGoldenRatio64 + (folded << s_HashCombineLeftShift) + (folded >> s_HashCombineRightShift);
    return folded;
}

[[nodiscard]] inline constexpr u64 UpdateFnv64NameHash(u64 hash, const NameHash& nameHash)noexcept{
    for(const u64 lane : nameHash.qwords)
        hash = UpdateFnv64U64LittleEndian(hash, lane);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameTextError{ enum Enum : u8{ NullText, EmbeddedNull }; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DebugNameCapacity = 256u;
inline constexpr usize s_NameHashBytes = sizeof(u64) * s_NameHashLaneCount;

static_assert(sizeof(NameHash) == s_NameHashBytes, "NameHash size must stay stable");

inline constexpr u64 s_LaneSeedSalt1 = 0x0123456789ABCDEFull;
inline constexpr u64 s_LaneSeedSalt2 = 0xFEDCBA9876543210ull;
inline constexpr u64 s_LaneSeedSalt3 = 0x0F1E2D3C4B5A6978ull;
inline constexpr u64 s_LaneSeedSalt4 = 0x8796A5B4C3D2E1F0ull;
inline constexpr u64 s_LaneSeedSalt5 = 0xDEADBEEFCAFEBABEull;
inline constexpr u64 s_LaneSeedSalt6 = 0x1234ABCD5678EF01ull;
inline constexpr u64 s_LaneSeedSalt7 = 0xA0B1C2D3E4F50617ull;
inline constexpr Array<u64, s_NameHashLaneCount> s_LaneSeeds = {
    s_Fnv64OffsetBasis,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt1,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt2,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt3,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt4,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt5,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt6,
    s_Fnv64OffsetBasis ^ s_LaneSeedSalt7,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(s_LaneSeeds) / sizeof(s_LaneSeeds[0]) == s_NameHashLaneCount, "s_LaneSeeds count must match s_NameHashLaneCount");

inline constexpr void InitializeNameHash(NameHash& hash)noexcept{
    for(u32 i = 0u; i < s_NameHashLaneCount; ++i)
        hash.qwords[i] = s_LaneSeeds[i];
}

template<typename CharT>
inline constexpr void UpdateCanonicalNameHashLanes(NameHash& hash, const CharT ch)noexcept(IsArithmetic_V<CharT>){
    const u64 byte = static_cast<u64>(static_cast<u8>(Canonicalize(ch)));
    for(u32 i = 0u; i < s_NameHashLaneCount; ++i){
        hash.qwords[i] ^= byte;
        hash.qwords[i] *= s_Fnv64Prime;
    }
}

template<typename CharT>
[[nodiscard]] inline constexpr b8 UpdateCanonicalNameHashText(NameHash& hash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    for(const CharT ch : text){
        if(ch == CharT{})
            return false;
        UpdateCanonicalNameHashLanes(hash, ch);
    }

    return true;
}

inline constexpr b8 EqualHash(const NameHash& a, const NameHash& b)noexcept{
    for(u32 i = 0u; i < s_NameHashLaneCount; ++i){
        if(a.qwords[i] != b.qwords[i])
            return false;
    }
    return true;
}

inline constexpr usize HashValue(const NameHash& hash)noexcept{
    usize seed = static_cast<usize>(hash.qwords[0]);
    for(u32 i = 1; i < s_NameHashLaneCount; ++i){
        HashCombineHash(seed, static_cast<usize>(hash.qwords[i]), static_cast<usize>(s_HashCombineGoldenRatio64));
    }
    return seed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr b8 IsZeroHash(const NameHash& hash)noexcept{
    for(u32 i = 0u; i < s_NameHashLaneCount; ++i){
        if(hash.qwords[i] != 0)
            return false;
    }
    return true;
}


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

[[nodiscard]] inline constexpr b8 LessNameHash(const NameHash& lhs, const NameHash& rhs)noexcept{
    for(u32 i = 0u; i < s_NameHashLaneCount; ++i){
        if(lhs.qwords[i] < rhs.qwords[i])
            return true;
        if(lhs.qwords[i] > rhs.qwords[i])
            return false;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr b8 operator==(const NameHash& a, const NameHash& b)noexcept{
    return NameDetail::EqualHash(a, b);
}
inline constexpr b8 operator!=(const NameHash& a, const NameHash& b)noexcept{
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

