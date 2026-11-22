// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "sha256.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Sha1Digest{
    static constexpr u32 s_ByteCount = 20u;
    static constexpr u32 s_HexTextLength = ShaBe32Detail::s_HexDigitsPerByte * s_ByteCount;

    u8 bytes[s_ByteCount] = {};
};

[[nodiscard]] inline b8 operator==(const Sha1Digest& lhs, const Sha1Digest& rhs)noexcept{
    return FOUNDATION_MEMCMP(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

[[nodiscard]] inline b8 operator!=(const Sha1Digest& lhs, const Sha1Digest& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Sha1Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_StateWordCount = 5u;
inline constexpr u32 s_RoundCount = 80u;
inline constexpr u32 s_ScheduleLagA = 3u;
inline constexpr u32 s_ScheduleLagB = 8u;
inline constexpr u32 s_ScheduleLagC = 14u;
inline constexpr u32 s_ScheduleLagD = 16u;
inline constexpr u32 s_ScheduleRotateLeft = 1u;
inline constexpr u32 s_RoundRotateLeftA = 5u;
inline constexpr u32 s_RoundRotateLeftB = 30u;
inline constexpr u32 s_RoundPhase1End = 20u;
inline constexpr u32 s_RoundPhase2End = 40u;
inline constexpr u32 s_RoundPhase3End = 60u;
inline constexpr u32 s_RoundConstantPhase1 = 0x5a827999u;
inline constexpr u32 s_RoundConstantPhase2 = 0x6ed9eba1u;
inline constexpr u32 s_RoundConstantPhase3 = 0x8f1bbcdcu;
inline constexpr u32 s_RoundConstantPhase4 = 0xca62c1d6u;

inline constexpr Array<u32, s_StateWordCount> s_InitialState = {
    0x67452301u,
    0xefcdab89u,
    0x98badcfeu,
    0x10325476u,
    0xc3d2e1f0u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 RotateLeft(const u32 value, const u32 count)noexcept{
    return (value << count) | (value >> (ShaBe32Detail::s_WordBits - count));
}

void CompressBlocks(Array<u32, s_StateWordCount>& state, const u8* blocks, usize blockCount)noexcept;
[[nodiscard]] Sha1Digest ComputeSha1Impl(BinaryByteView input)noexcept;

inline void Compress(Array<u32, s_StateWordCount>& state, const u8* const block)noexcept{
    CompressBlocks(state, block, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Sha1Digest ComputeSha1(const BinaryByteView input)noexcept{
    return Sha1Detail::ComputeSha1Impl(input);
}

[[nodiscard]] inline Sha1Digest ComputeSha1Text(const AStringView text)noexcept{
    return ComputeSha1(BinaryByteView{ reinterpret_cast<const u8*>(text.data()), text.size() });
}

template<typename StringT>
inline void AppendSha1Hex(const Sha1Digest& digest, StringT& outText){
    for(u32 index = 0u; index < Sha1Digest::s_ByteCount; ++index){
        AppendHexByte(digest.bytes[index], outText);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

