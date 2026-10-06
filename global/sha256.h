// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Sha256Digest{
    u8 bytes[32u] = {};
};

[[nodiscard]] inline bool operator==(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return GLB_MEMCMP(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

[[nodiscard]] inline bool operator!=(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Sha256Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RoundConstants[64u] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 Rotate(const u32 value, const u32 count)noexcept{
    return (value >> count) | (value << (32u - count));
}

inline void Compress(u32 (&state)[8u], const u8 (&block)[64u])noexcept{
    u32 words[64u] = {};
    for(u32 index = 0u; index < 16u; ++index){
        const u8* bytes = block + index * 4u;
        words[index] = (static_cast<u32>(bytes[0u]) << 24u) | (static_cast<u32>(bytes[1u]) << 16u) | (static_cast<u32>(bytes[2u]) << 8u) | static_cast<u32>(bytes[3u]);
    }
    for(u32 index = 16u; index < 64u; ++index){
        const u32 a = words[index - 15u];
        const u32 b = words[index - 2u];
        words[index] = words[index - 16u] + (Rotate(a, 7u) ^ Rotate(a, 18u) ^ (a >> 3u))
            + words[index - 7u] + (Rotate(b, 17u) ^ Rotate(b, 19u) ^ (b >> 10u));
    }
    u32 a = state[0u], b = state[1u], c = state[2u], d = state[3u];
    u32 e = state[4u], f = state[5u], g = state[6u], h = state[7u];
    for(u32 index = 0u; index < 64u; ++index){
        const u32 first = h + (Rotate(e, 6u) ^ Rotate(e, 11u) ^ Rotate(e, 25u))
            + ((e & f) ^ (~e & g)) + s_RoundConstants[index] + words[index];
        const u32 second = (Rotate(a, 2u) ^ Rotate(a, 13u) ^ Rotate(a, 22u)) + ((a & b) ^ (a & c) ^ (b & c));
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
    u32 state[8u] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    u8 block[64u] = {};
    usize offset = 0u;
    while(input.size() - offset >= sizeof(block)){
        GLB_MEMCPY(block, sizeof(block), input.data() + offset, sizeof(block));
        Sha256Detail::Compress(state, block);
        offset += sizeof(block);
    }
    const usize remainder = input.size() - offset;
    GLB_MEMSET(block, 0, sizeof(block));
    if(remainder > 0u)
        GLB_MEMCPY(block, sizeof(block), input.data() + offset, remainder);
    block[remainder] = 0x80u;
    if(remainder >= 56u){
        Sha256Detail::Compress(state, block);
        GLB_MEMSET(block, 0, sizeof(block));
    }
    const u64 bitCount = static_cast<u64>(input.size()) * 8u;
    for(u32 index = 0u; index < 8u; ++index)
        block[63u - index] = static_cast<u8>(bitCount >> (index * 8u));
    Sha256Detail::Compress(state, block);
    Sha256Digest digest;
    for(u32 index = 0u; index < 32u; ++index)
        digest.bytes[index] = static_cast<u8>(state[index / 4u] >> ((3u - index % 4u) * 8u));
    return digest;
}

[[nodiscard]] inline bool ParseSha256(const AStringView text, Sha256Digest& outDigest)noexcept{
    if(text.size() != 64u)
        return false;
    Sha256Digest parsed;
    for(u32 index = 0u; index < 32u; ++index){
        u32 value = 0u;
        for(u32 digit = 0u; digit < 2u; ++digit){
            const char character = text[index * 2u + digit];
            if(character >= '0' && character <= '9')
                value = value * 16u + static_cast<u32>(character - '0');
            else if(character >= 'a' && character <= 'f')
                value = value * 16u + static_cast<u32>(character - 'a') + 10u;
            else
                return false;
        }
        parsed.bytes[index] = static_cast<u8>(value);
    }
    outDigest = parsed;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

