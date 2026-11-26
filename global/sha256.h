// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binary.h"
#include "binary_big_endian.h"
#include "expected.h"
#include "not_null.h"
#include "numeric_text.h"

#include <bit>
#include <cstring>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Sha256Digest{
    static constexpr u32 s_ByteCount = 32u;
    static constexpr u32 s_HexTextLength = 64u;

    u8 bytes[s_ByteCount] = {};
};

[[nodiscard]] inline b8 operator==(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return FOUNDATION_MEMCMP(lhs.bytes, rhs.bytes, sizeof(lhs.bytes)) == 0;
}

[[nodiscard]] inline b8 operator!=(const Sha256Digest& lhs, const Sha256Digest& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaBe32Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_WordBits = 32u;
inline constexpr u32 s_WordByteCount = 4u;
inline constexpr u32 s_LastByteInWord = 3u;
inline constexpr u32 s_BlockByteCount = 64u;
inline constexpr u32 s_BlockWordCount = 16u;
inline constexpr u32 s_LengthFieldByteCount = 8u;
inline constexpr u32 s_LengthFieldOffset = s_BlockByteCount - s_LengthFieldByteCount;
inline constexpr u8 s_PaddingMarker = 0x80u;
inline constexpr u32 s_HexDigitsPerByte = 2u;

[[nodiscard]] constexpr inline u32 ByteSwap32(const u32 value)noexcept{
    return __builtin_bswap32(value);
}

[[nodiscard]] constexpr inline u64 ByteSwap64(const u64 value)noexcept{
    return __builtin_bswap64(value);
}

[[nodiscard]] inline usize PrepareShaBe32TailBlocks(
    const u8* const inputData,
    const usize inputSize,
    const usize offset,
    Array<u8, s_BlockByteCount * 2u>& outTail
)noexcept{
    const usize remainder = inputSize - offset;
    if(remainder > 0u)
        std::memcpy(outTail.data(), inputData + offset, remainder);
    outTail[remainder] = s_PaddingMarker;

    const usize tailBlockCount = remainder >= s_LengthFieldOffset ? 2u : 1u;
    const usize totalTailBytes = tailBlockCount * s_BlockByteCount;
    const usize lengthOffset = totalTailBytes - s_LengthFieldByteCount;
    if(lengthOffset > remainder + 1u)
        std::memset(outTail.data() + remainder + 1u, 0, lengthOffset - (remainder + 1u));

    const u64 bitCount = static_cast<u64>(inputSize) * s_BitsPerByte;
    if constexpr(std::endian::native == std::endian::little){
        const u64 bigEndianBitCount = ByteSwap64(bitCount);
        std::memcpy(outTail.data() + lengthOffset, &bigEndianBitCount, sizeof(u64));
    }
    else{
        for(u32 index = 0u; index < s_LengthFieldByteCount; ++index)
            outTail[totalTailBytes - 1u - index] = static_cast<u8>(bitCount >> (index * s_BitsPerByte));
    }
    return tailBlockCount;
}

template<typename DigestT, u32 StateWordCount>
[[nodiscard]] inline DigestT FinalizeShaBe32Digest(const Array<u32, StateWordCount>& state)noexcept{
    static_assert(DigestT::s_ByteCount == StateWordCount * s_WordByteCount);

    DigestT digest;
    if constexpr(std::endian::native == std::endian::little){
        for(u32 index = 0u; index < StateWordCount; ++index){
            const u32 bigEndianWord = ByteSwap32(state[index]);
            std::memcpy(digest.bytes + index * s_WordByteCount, &bigEndianWord, sizeof(u32));
        }
    }
    else{
        for(u32 index = 0u; index < DigestT::s_ByteCount; ++index)
            digest.bytes[index] = static_cast<u8>(state[index / s_WordByteCount] >> ((s_LastByteInWord - index % s_WordByteCount) * s_BitsPerByte));
    }
    return digest;
}

inline void UnpackBlockWords(const u8* const block, u32* const outWords)noexcept{
    if constexpr(std::endian::native == std::endian::little){
        for(u32 index = 0u; index < s_BlockWordCount; ++index){
            u32 word = 0u;
            std::memcpy(&word, block + index * s_WordByteCount, sizeof(u32));
            outWords[index] = ByteSwap32(word);
        }
    }
    else{
        for(u32 index = 0u; index < s_BlockWordCount; ++index)
            outWords[index] = ReadU32BigEndian(MakeNotNull(block + index * s_WordByteCount));
    }
}

#if defined(__x86_64__) || defined(_M_X64)
[[nodiscard]] b8 HasX86ShaNi()noexcept;
#endif

template<typename DigestT, u32 StateWordCount, void (*CompressFn)(Array<u32, StateWordCount>&, const u8*, usize)>
[[nodiscard]] inline DigestT ComputeShaBe32WithKernel(
    const Array<u32, StateWordCount>& initialState,
    const BinaryByteView input
)noexcept{
    Array<u32, StateWordCount> state = initialState;
    const usize inputSize = input.size();
    const u8* const inputData = input.data();
    const usize fullBlocks = inputSize / s_BlockByteCount;
    const usize offset = fullBlocks * s_BlockByteCount;
    if(fullBlocks > 0u)
        CompressFn(state, inputData, fullBlocks);

    alignas(16) Array<u8, s_BlockByteCount * 2u> tail;
    const usize tailBlockCount = PrepareShaBe32TailBlocks(inputData, inputSize, offset, tail);
    CompressFn(state, tail.data(), tailBlockCount);
    return FinalizeShaBe32Digest<DigestT, StateWordCount>(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Sha256Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_StateWordCount = 8u;
inline constexpr u32 s_RoundCount = 64u;
inline constexpr u32 s_HexRadix = 16u;
inline constexpr u32 s_HexLetterBase = 10u;
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


alignas(16) inline constexpr Array<u32, s_RoundCount> s_RoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline constexpr Array<u32, s_StateWordCount> s_InitialState = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 Rotate(const u32 value, const u32 count)noexcept{
    return (value >> count) | (value << (ShaBe32Detail::s_WordBits - count));
}

[[nodiscard]] Sha256Digest ComputeSha256Impl(BinaryByteView input)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Sha256Digest ComputeSha256(const BinaryByteView input)noexcept{
    return Sha256Detail::ComputeSha256Impl(input);
}

[[nodiscard]] inline Expected<Sha256Digest> ParseSha256(const AStringView text)noexcept{
    if(text.size() != Sha256Digest::s_HexTextLength)
        return MakeUnexpected(Failure{});
    Sha256Digest parsed;
    for(u32 index = 0u; index < Sha256Digest::s_ByteCount; ++index){
        u32 value = 0u;
        for(u32 digit = 0u; digit < ShaBe32Detail::s_HexDigitsPerByte; ++digit){
            const char character = text[index * ShaBe32Detail::s_HexDigitsPerByte + digit];
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

