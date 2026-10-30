// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "sha256.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Sha1Digest{
    static constexpr u32 s_ByteCount = 20u;

    u8 bytes[s_ByteCount] = {};
};

[[nodiscard]] inline b8 operator==(const Sha1Digest& lhs, const Sha1Digest& rhs)noexcept{
    return NWB_MEMCMP(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

[[nodiscard]] inline b8 operator!=(const Sha1Digest& lhs, const Sha1Digest& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Sha1Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ShaBe32Detail::s_WordBits;
using ShaBe32Detail::s_BlockByteCount;
using ShaBe32Detail::s_BlockWordCount;
using ShaBe32Detail::s_HexDigitsPerByte;

inline constexpr u32 s_StateWordCount = 5u;
inline constexpr u32 s_RoundCount = 80u;
inline constexpr u32 s_HexNibbleBits = 4u;
inline constexpr u8 s_HexNibbleMask = 0x0fu;
inline constexpr u32 s_HexTextLength = s_HexDigitsPerByte * Sha1Digest::s_ByteCount;
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
inline constexpr AStringView s_HexDigits = "0123456789abcdef";

inline constexpr u32 s_InitialState[s_StateWordCount] = {
    0x67452301u,
    0xefcdab89u,
    0x98badcfeu,
    0x10325476u,
    0xc3d2e1f0u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 RotateLeft(const u32 value, const u32 count)noexcept{
    return (value << count) | (value >> (s_WordBits - count));
}

inline void Compress(u32 (&state)[s_StateWordCount], const u8 (&block)[s_BlockByteCount])noexcept{
    u32 words[s_RoundCount];
    ShaBe32Detail::UnpackBlockWords(block, words);
    for(u32 index = s_BlockWordCount; index < s_RoundCount; ++index){
        const u32 mixed = words[index - s_ScheduleLagA] ^ words[index - s_ScheduleLagB] ^ words[index - s_ScheduleLagC] ^ words[index - s_ScheduleLagD];
        words[index] = RotateLeft(mixed, s_ScheduleRotateLeft);
    }

    u32 a = state[0u];
    u32 b = state[1u];
    u32 c = state[2u];
    u32 d = state[3u];
    u32 e = state[4u];
    for(u32 index = 0u; index < s_RoundCount; ++index){
        u32 f = 0u;
        u32 k = 0u;
        if(index < s_RoundPhase1End){
            f = (b & c) | (~b & d);
            k = s_RoundConstantPhase1;
        }
        else if(index < s_RoundPhase2End){
            f = b ^ c ^ d;
            k = s_RoundConstantPhase2;
        }
        else if(index < s_RoundPhase3End){
            f = (b & c) | (b & d) | (c & d);
            k = s_RoundConstantPhase3;
        }
        else{
            f = b ^ c ^ d;
            k = s_RoundConstantPhase4;
        }

        const u32 next = RotateLeft(a, s_RoundRotateLeftA) + f + e + k + words[index];
        e = d;
        d = c;
        c = RotateLeft(b, s_RoundRotateLeftB);
        b = a;
        a = next;
    }

    state[0u] += a;
    state[1u] += b;
    state[2u] += c;
    state[3u] += d;
    state[4u] += e;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Byte-oriented SHA-1 content identity; no host byte-order or allocator dependency.
[[nodiscard]] inline Sha1Digest ComputeSha1(const BinaryByteView input)noexcept{
    return ShaBe32Detail::ComputeDigest<Sha1Digest, Sha1Detail::s_StateWordCount, &Sha1Detail::Compress>(input, Sha1Detail::s_InitialState);
}

[[nodiscard]] inline Sha1Digest ComputeSha1Text(const AStringView text)noexcept{
    return ComputeSha1(BinaryByteView{ reinterpret_cast<const u8*>(text.data()), text.size() });
}

template<typename StringT>
inline void AppendSha1Hex(const Sha1Digest& digest, StringT& outText){
    for(u32 index = 0u; index < Sha1Digest::s_ByteCount; ++index){
        const u8 byte = digest.bytes[index];
        outText += Sha1Detail::s_HexDigits[byte >> Sha1Detail::s_HexNibbleBits];
        outText += Sha1Detail::s_HexDigits[byte & Sha1Detail::s_HexNibbleMask];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

