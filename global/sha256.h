// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"
#include "expected.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Sha256Digest{
    static constexpr u32 s_ByteCount = 32u;

    u8 bytes[s_ByteCount] = {};
};

[[nodiscard]] inline bool operator==(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return NWB_MEMCMP(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

[[nodiscard]] inline bool operator!=(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Sha256Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_WordBits = 32u;
inline constexpr u32 s_WordByteCount = 4u;
inline constexpr u32 s_LastByteInWord = 3u;
inline constexpr u32 s_StateWordCount = 8u;
inline constexpr u32 s_BlockByteCount = 64u;
inline constexpr u32 s_BlockWordCount = 16u;
inline constexpr u32 s_RoundCount = 64u;
inline constexpr u32 s_LengthFieldByteCount = 8u;
inline constexpr u32 s_LengthFieldOffset = s_BlockByteCount - s_LengthFieldByteCount;
inline constexpr u8 s_PaddingMarker = 0x80u;
inline constexpr u32 s_HexRadix = 16u;
inline constexpr u32 s_HexLetterBase = 10u;
inline constexpr u32 s_HexDigitsPerByte = 2u;
inline constexpr u32 s_HexTextLength = s_HexDigitsPerByte * Sha256Digest::s_ByteCount;
inline constexpr u32 s_FirstByteShift = 24u;
inline constexpr u32 s_SecondByteShift = 16u;
inline constexpr u32 s_ThirdByteShift = 8u;
inline constexpr u32 s_ScheduleLagSigma0 = 15u;
inline constexpr u32 s_ScheduleLagSigma1 = 2u;
inline constexpr u32 s_ScheduleLagPrevious = 16u;
inline constexpr u32 s_ScheduleLagAdded = 7u;
inline constexpr u32 s_ScheduleSigma0RotateA = 7u;
inline constexpr u32 s_ScheduleSigma0RotateB = 18u;
inline constexpr u32 s_ScheduleSigma0Shift = 3u;
inline constexpr u32 s_ScheduleSigma1RotateA = 17u;
inline constexpr u32 s_ScheduleSigma1RotateB = 19u;
inline constexpr u32 s_ScheduleSigma1Shift = 10u;
inline constexpr u32 s_RoundSigma0RotateA = 2u;
inline constexpr u32 s_RoundSigma0RotateB = 13u;
inline constexpr u32 s_RoundSigma0RotateC = 22u;
inline constexpr u32 s_RoundSigma1RotateA = 6u;
inline constexpr u32 s_RoundSigma1RotateB = 11u;
inline constexpr u32 s_RoundSigma1RotateC = 25u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RoundConstants[s_RoundCount] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline constexpr u32 s_InitialState[s_StateWordCount] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 Rotate(const u32 value, const u32 count)noexcept{
    return (value >> count) | (value << (s_WordBits - count));
}

inline void Compress(u32 (&state)[s_StateWordCount], const u8 (&block)[s_BlockByteCount])noexcept{
    u32 words[s_RoundCount];
    for(u32 index = 0u; index < s_BlockWordCount; ++index){
        const u8* bytes = block + index * s_WordByteCount;
        words[index] = (static_cast<u32>(bytes[0u]) << s_FirstByteShift) | (static_cast<u32>(bytes[1u]) << s_SecondByteShift) | (static_cast<u32>(bytes[2u]) << s_ThirdByteShift) | static_cast<u32>(bytes[3u]);
    }
    for(u32 index = s_BlockWordCount; index < s_RoundCount; ++index){
        const u32 a = words[index - s_ScheduleLagSigma0];
        const u32 b = words[index - s_ScheduleLagSigma1];
        words[index] = words[index - s_ScheduleLagPrevious] + (Rotate(a, s_ScheduleSigma0RotateA) ^ Rotate(a, s_ScheduleSigma0RotateB) ^ (a >> s_ScheduleSigma0Shift)) + words[index - s_ScheduleLagAdded] + (Rotate(b, s_ScheduleSigma1RotateA) ^ Rotate(b, s_ScheduleSigma1RotateB) ^ (b >> s_ScheduleSigma1Shift));
    }
    u32 a = state[0u], b = state[1u], c = state[2u], d = state[3u];
    u32 e = state[4u], f = state[5u], g = state[6u], h = state[7u];
    for(u32 index = 0u; index < s_RoundCount; ++index){
        const u32 first = h + (Rotate(e, s_RoundSigma1RotateA) ^ Rotate(e, s_RoundSigma1RotateB) ^ Rotate(e, s_RoundSigma1RotateC)) + ((e & f) ^ (~e & g)) + s_RoundConstants[index] + words[index];
        const u32 second = (Rotate(a, s_RoundSigma0RotateA) ^ Rotate(a, s_RoundSigma0RotateB) ^ Rotate(a, s_RoundSigma0RotateC)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + first; d = c; c = b; b = a; a = first + second;
    }
    state[0u] += a; state[1u] += b; state[2u] += c; state[3u] += d;
    state[4u] += e; state[5u] += f; state[6u] += g; state[7u] += h;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Byte-oriented SHA-256 content identity; no host byte-order or allocator dependency.
[[nodiscard]] inline Sha256Digest ComputeSha256(const BinaryByteView input)noexcept{
    u32 state[Sha256Detail::s_StateWordCount];
    NWB_MEMCPY(state, sizeof(state), Sha256Detail::s_InitialState, sizeof(state));
    u8 block[Sha256Detail::s_BlockByteCount];
    usize offset = 0u;
    while(input.size() - offset >= sizeof(block)){
        NWB_MEMCPY(block, sizeof(block), input.data() + offset, sizeof(block));
        Sha256Detail::Compress(state, block);
        offset += sizeof(block);
    }
    const usize remainder = input.size() - offset;
    NWB_MEMSET(block, 0, sizeof(block));
    if(remainder > 0u)
        NWB_MEMCPY(block, sizeof(block), input.data() + offset, remainder);
    block[remainder] = Sha256Detail::s_PaddingMarker;
    if(remainder >= Sha256Detail::s_LengthFieldOffset){
        Sha256Detail::Compress(state, block);
        NWB_MEMSET(block, 0, sizeof(block));
    }
    const u64 bitCount = static_cast<u64>(input.size()) * s_BitsPerByte;
    for(u32 index = 0u; index < Sha256Detail::s_LengthFieldByteCount; ++index)
        block[Sha256Detail::s_BlockByteCount - 1u - index] = static_cast<u8>(bitCount >> (index * s_BitsPerByte));
    Sha256Detail::Compress(state, block);
    Sha256Digest digest;
    for(u32 index = 0u; index < Sha256Digest::s_ByteCount; ++index)
        digest.bytes[index] = static_cast<u8>(state[index / Sha256Detail::s_WordByteCount] >> ((Sha256Detail::s_LastByteInWord - index % Sha256Detail::s_WordByteCount) * s_BitsPerByte));
    return digest;
}

[[nodiscard]] inline Expected<Sha256Digest> ParseSha256(const AStringView text)noexcept{
    if(text.size() != Sha256Detail::s_HexTextLength)
        return MakeUnexpected(Failure{});
    Sha256Digest parsed;
    for(u32 index = 0u; index < Sha256Digest::s_ByteCount; ++index){
        u32 value = 0u;
        for(u32 digit = 0u; digit < Sha256Detail::s_HexDigitsPerByte; ++digit){
            const char character = text[index * Sha256Detail::s_HexDigitsPerByte + digit];
            if(character >= '0' && character <= '9')
                value = value * Sha256Detail::s_HexRadix + static_cast<u32>(character - '0');
            else if(character >= 'a' && character <= 'f')
                value = value * Sha256Detail::s_HexRadix + static_cast<u32>(character - 'a') + Sha256Detail::s_HexLetterBase;
            else
                return MakeUnexpected(Failure{});
        }
        parsed.bytes[index] = static_cast<u8>(value);
    }
    return parsed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

