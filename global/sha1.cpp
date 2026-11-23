// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sha1.h"

#include "platform.h"

#include <cstring>

#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(FOUNDATION_PLATFORM_LINUX)
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif
#elif defined(FOUNDATION_ARCH_X86_64) || defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_sha1{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
[[nodiscard]] b8 DetectArmv8Sha1()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(FOUNDATION_PLATFORM_LINUX)
#if defined(HWCAP_SHA1)
    return (::getauxval(AT_HWCAP) & HWCAP_SHA1) != 0ul;
#else
    return false;
#endif
#else
    return false;
#endif
}

[[nodiscard]] inline b8 HasArmv8Sha1()noexcept{
    static const b8 s_Supported = DetectArmv8Sha1();
    return s_Supported;
}

[[gnu::target("sha2,neon")]] void CompressBlocksArmv8Sha1(
    Array<u32, Sha1Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    const uint32x4_t k0 = vdupq_n_u32(Sha1Detail::s_RoundConstantPhase1);
    const uint32x4_t k1 = vdupq_n_u32(Sha1Detail::s_RoundConstantPhase2);
    const uint32x4_t k2 = vdupq_n_u32(Sha1Detail::s_RoundConstantPhase3);
    const uint32x4_t k3 = vdupq_n_u32(Sha1Detail::s_RoundConstantPhase4);

    uint32x4_t abcd = vld1q_u32(state.data());
    u32 e = state[4u];

    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        const uint32x4_t abcdSave = abcd;
        const u32 eSave = e;

        uint32x4_t msg0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 0u)));
        uint32x4_t msg1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 16u)));
        uint32x4_t msg2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 32u)));
        uint32x4_t msg3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 48u)));

        uint32x4_t tmp0 = vaddq_u32(msg0, k0);
        u32 e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1cq_u32(abcd, e, tmp0);
        uint32x4_t tmp1 = vaddq_u32(msg1, k0);
        msg0 = vsha1su0q_u32(msg0, msg1, msg2);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1cq_u32(abcd, e1, tmp1);
        msg0 = vsha1su1q_u32(msg0, msg3);
        tmp0 = vaddq_u32(msg2, k0);
        msg1 = vsha1su0q_u32(msg1, msg2, msg3);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1cq_u32(abcd, e, tmp0);
        msg1 = vsha1su1q_u32(msg1, msg0);
        tmp1 = vaddq_u32(msg3, k0);
        msg2 = vsha1su0q_u32(msg2, msg3, msg0);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1cq_u32(abcd, e1, tmp1);
        msg2 = vsha1su1q_u32(msg2, msg1);
        tmp0 = vaddq_u32(msg0, k0);
        msg3 = vsha1su0q_u32(msg3, msg0, msg1);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1cq_u32(abcd, e, tmp0);
        msg3 = vsha1su1q_u32(msg3, msg2);
        tmp1 = vaddq_u32(msg1, k1);
        msg0 = vsha1su0q_u32(msg0, msg1, msg2);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);
        msg0 = vsha1su1q_u32(msg0, msg3);
        tmp0 = vaddq_u32(msg2, k1);
        msg1 = vsha1su0q_u32(msg1, msg2, msg3);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e, tmp0);
        msg1 = vsha1su1q_u32(msg1, msg0);
        tmp1 = vaddq_u32(msg3, k1);
        msg2 = vsha1su0q_u32(msg2, msg3, msg0);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);
        msg2 = vsha1su1q_u32(msg2, msg1);
        tmp0 = vaddq_u32(msg0, k1);
        msg3 = vsha1su0q_u32(msg3, msg0, msg1);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e, tmp0);
        msg3 = vsha1su1q_u32(msg3, msg2);
        tmp1 = vaddq_u32(msg1, k1);
        msg0 = vsha1su0q_u32(msg0, msg1, msg2);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);
        msg0 = vsha1su1q_u32(msg0, msg3);
        tmp0 = vaddq_u32(msg2, k2);
        msg1 = vsha1su0q_u32(msg1, msg2, msg3);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1mq_u32(abcd, e, tmp0);
        msg1 = vsha1su1q_u32(msg1, msg0);
        tmp1 = vaddq_u32(msg3, k2);
        msg2 = vsha1su0q_u32(msg2, msg3, msg0);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1mq_u32(abcd, e1, tmp1);
        msg2 = vsha1su1q_u32(msg2, msg1);
        tmp0 = vaddq_u32(msg0, k2);
        msg3 = vsha1su0q_u32(msg3, msg0, msg1);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1mq_u32(abcd, e, tmp0);
        msg3 = vsha1su1q_u32(msg3, msg2);
        tmp1 = vaddq_u32(msg1, k2);
        msg0 = vsha1su0q_u32(msg0, msg1, msg2);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1mq_u32(abcd, e1, tmp1);
        msg0 = vsha1su1q_u32(msg0, msg3);
        tmp0 = vaddq_u32(msg2, k2);
        msg1 = vsha1su0q_u32(msg1, msg2, msg3);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1mq_u32(abcd, e, tmp0);
        msg1 = vsha1su1q_u32(msg1, msg0);
        tmp1 = vaddq_u32(msg3, k3);
        msg2 = vsha1su0q_u32(msg2, msg3, msg0);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);
        msg2 = vsha1su1q_u32(msg2, msg1);
        tmp0 = vaddq_u32(msg0, k3);
        msg3 = vsha1su0q_u32(msg3, msg0, msg1);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e, tmp0);
        msg3 = vsha1su1q_u32(msg3, msg2);
        tmp1 = vaddq_u32(msg1, k3);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);
        tmp0 = vaddq_u32(msg2, k3);

        e1 = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e, tmp0);
        tmp1 = vaddq_u32(msg3, k3);

        e = vsha1h_u32(vgetq_lane_u32(abcd, 0));
        abcd = vsha1pq_u32(abcd, e1, tmp1);

        abcd = vaddq_u32(abcd, abcdSave);
        e += eSave;
        blocks += ShaBe32Detail::s_BlockByteCount;
    }

    vst1q_u32(state.data(), abcd);
    state[4u] = e;
}
#elif defined(FOUNDATION_ARCH_X86_64) || defined(__x86_64__) || defined(_M_X64)
[[nodiscard]] b8 DetectX86ShaNi()noexcept{
#if defined(_MSC_VER)
    int info[4] = {};
    ::__cpuidex(info, 0, 0);
    if(info[0] < 7)
        return false;
    int ext[4] = {};
    ::__cpuidex(ext, 7, 0);
    return (ext[1] & (1 << 29)) != 0;
#else
    unsigned int eax = 0u;
    unsigned int ebx = 0u;
    unsigned int ecx = 0u;
    unsigned int edx = 0u;
    if(__get_cpuid_count(7u, 0u, &eax, &ebx, &ecx, &edx) == 0)
        return false;
    return (ebx & (1u << 29u)) != 0u;
#endif
}

[[nodiscard]] inline b8 HasX86ShaNi()noexcept{
    static const b8 s_Supported = DetectX86ShaNi();
    return s_Supported;
}

[[gnu::target("sha,sse4.1")]] void CompressBlocksX86ShaNi(
    Array<u32, Sha1Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    const __m128i bswapMask = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i abcd = _mm_set_epi32(static_cast<int>(state[0u]), static_cast<int>(state[1u]), static_cast<int>(state[2u]), static_cast<int>(state[3u]));
    __m128i e0 = _mm_set_epi32(static_cast<int>(state[4u]), 0, 0, 0);

    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        const __m128i abcdSave = abcd;
        const __m128i eSave = e0;

        __m128i msg0 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 0u)), bswapMask);
        __m128i e1 = _mm_add_epi32(e0, msg0);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);

        __m128i msg1 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 16u)), bswapMask);
        e1 = _mm_sha1nexte_epu32(e0, msg1);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
        msg0 = _mm_sha1msg1_epu32(msg0, msg1);

        __m128i msg2 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 32u)), bswapMask);
        e1 = _mm_sha1nexte_epu32(e0, msg2);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
        msg1 = _mm_sha1msg1_epu32(msg1, msg2);
        msg0 = _mm_xor_si128(msg0, msg2);

        __m128i msg3 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 48u)), bswapMask);
        e1 = _mm_sha1nexte_epu32(e0, msg3);
        e0 = abcd;
        msg0 = _mm_sha1msg2_epu32(msg0, msg3);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
        msg2 = _mm_sha1msg1_epu32(msg2, msg3);
        msg1 = _mm_xor_si128(msg1, msg3);

        e1 = _mm_sha1nexte_epu32(e0, msg0);
        e0 = abcd;
        msg1 = _mm_sha1msg2_epu32(msg1, msg0);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 0);
        msg3 = _mm_sha1msg1_epu32(msg3, msg0);
        msg2 = _mm_xor_si128(msg2, msg0);

        e1 = _mm_sha1nexte_epu32(e0, msg1);
        e0 = abcd;
        msg2 = _mm_sha1msg2_epu32(msg2, msg1);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
        msg0 = _mm_sha1msg1_epu32(msg0, msg1);
        msg3 = _mm_xor_si128(msg3, msg1);

        e1 = _mm_sha1nexte_epu32(e0, msg2);
        e0 = abcd;
        msg3 = _mm_sha1msg2_epu32(msg3, msg2);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
        msg1 = _mm_sha1msg1_epu32(msg1, msg2);
        msg0 = _mm_xor_si128(msg0, msg2);

        e1 = _mm_sha1nexte_epu32(e0, msg3);
        e0 = abcd;
        msg0 = _mm_sha1msg2_epu32(msg0, msg3);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
        msg2 = _mm_sha1msg1_epu32(msg2, msg3);
        msg1 = _mm_xor_si128(msg1, msg3);

        e1 = _mm_sha1nexte_epu32(e0, msg0);
        e0 = abcd;
        msg1 = _mm_sha1msg2_epu32(msg1, msg0);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
        msg3 = _mm_sha1msg1_epu32(msg3, msg0);
        msg2 = _mm_xor_si128(msg2, msg0);

        e1 = _mm_sha1nexte_epu32(e0, msg1);
        e0 = abcd;
        msg2 = _mm_sha1msg2_epu32(msg2, msg1);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 1);
        msg0 = _mm_sha1msg1_epu32(msg0, msg1);
        msg3 = _mm_xor_si128(msg3, msg1);

        e1 = _mm_sha1nexte_epu32(e0, msg2);
        e0 = abcd;
        msg3 = _mm_sha1msg2_epu32(msg3, msg2);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
        msg1 = _mm_sha1msg1_epu32(msg1, msg2);
        msg0 = _mm_xor_si128(msg0, msg2);

        e1 = _mm_sha1nexte_epu32(e0, msg3);
        e0 = abcd;
        msg0 = _mm_sha1msg2_epu32(msg0, msg3);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
        msg2 = _mm_sha1msg1_epu32(msg2, msg3);
        msg1 = _mm_xor_si128(msg1, msg3);

        e1 = _mm_sha1nexte_epu32(e0, msg0);
        e0 = abcd;
        msg1 = _mm_sha1msg2_epu32(msg1, msg0);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
        msg3 = _mm_sha1msg1_epu32(msg3, msg0);
        msg2 = _mm_xor_si128(msg2, msg0);

        e1 = _mm_sha1nexte_epu32(e0, msg1);
        e0 = abcd;
        msg2 = _mm_sha1msg2_epu32(msg2, msg1);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
        msg0 = _mm_sha1msg1_epu32(msg0, msg1);
        msg3 = _mm_xor_si128(msg3, msg1);

        e1 = _mm_sha1nexte_epu32(e0, msg2);
        e0 = abcd;
        msg3 = _mm_sha1msg2_epu32(msg3, msg2);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 2);
        msg1 = _mm_sha1msg1_epu32(msg1, msg2);
        msg0 = _mm_xor_si128(msg0, msg2);

        e1 = _mm_sha1nexte_epu32(e0, msg3);
        e0 = abcd;
        msg0 = _mm_sha1msg2_epu32(msg0, msg3);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
        msg2 = _mm_sha1msg1_epu32(msg2, msg3);
        msg1 = _mm_xor_si128(msg1, msg3);

        e1 = _mm_sha1nexte_epu32(e0, msg0);
        e0 = abcd;
        msg1 = _mm_sha1msg2_epu32(msg1, msg0);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
        msg3 = _mm_sha1msg1_epu32(msg3, msg0);
        msg2 = _mm_xor_si128(msg2, msg0);

        e1 = _mm_sha1nexte_epu32(e0, msg1);
        e0 = abcd;
        msg2 = _mm_sha1msg2_epu32(msg2, msg1);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);
        msg3 = _mm_xor_si128(msg3, msg1);

        e1 = _mm_sha1nexte_epu32(e0, msg2);
        e0 = abcd;
        msg3 = _mm_sha1msg2_epu32(msg3, msg2);
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

        e1 = _mm_sha1nexte_epu32(e0, msg3);
        e0 = abcd;
        abcd = _mm_sha1rnds4_epu32(abcd, e1, 3);

        e0 = _mm_sha1nexte_epu32(e0, eSave);
        abcd = _mm_add_epi32(abcd, abcdSave);
        blocks += ShaBe32Detail::s_BlockByteCount;
    }

    const __m128i storedAbcd = _mm_shuffle_epi32(abcd, 0x1B);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(state.data()), storedAbcd);
    state[4u] = static_cast<u32>(_mm_extract_epi32(e0, 3));
}
#endif

template<u32 Phase>
inline void ScalarRoundStep(const u32 a, u32& b, const u32 c, const u32 d, u32& e, const u32 word)noexcept{
    u32 f = 0u;
    u32 k = 0u;
    if constexpr(Phase == 0u){
        f = (b & c) | (~b & d);
        k = Sha1Detail::s_RoundConstantPhase1;
    }
    else if constexpr(Phase == 1u){
        f = b ^ c ^ d;
        k = Sha1Detail::s_RoundConstantPhase2;
    }
    else if constexpr(Phase == 2u){
        f = (b & c) | (b & d) | (c & d);
        k = Sha1Detail::s_RoundConstantPhase3;
    }
    else{
        f = b ^ c ^ d;
        k = Sha1Detail::s_RoundConstantPhase4;
    }
    e += Sha1Detail::RotateLeft(a, Sha1Detail::s_RoundRotateLeftA) + f + k + word;
    b = Sha1Detail::RotateLeft(b, Sha1Detail::s_RoundRotateLeftB);
}

void CompressBlocksScalar(
    Array<u32, Sha1Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        Array<u32, Sha1Detail::s_RoundCount> words;
        ShaBe32Detail::UnpackBlockWords(blocks, words.data());
        for(u32 index = ShaBe32Detail::s_BlockWordCount; index < Sha1Detail::s_RoundCount; ++index){
            const u32 mixed = words[index - Sha1Detail::s_ScheduleLagA] ^ words[index - Sha1Detail::s_ScheduleLagB] ^ words[index - Sha1Detail::s_ScheduleLagC] ^ words[index - Sha1Detail::s_ScheduleLagD];
            words[index] = Sha1Detail::RotateLeft(mixed, Sha1Detail::s_ScheduleRotateLeft);
        }

        u32 a = state[0u];
        u32 b = state[1u];
        u32 c = state[2u];
        u32 d = state[3u];
        u32 e = state[4u];
        for(u32 index = 0u; index < Sha1Detail::s_RoundPhase1End; index += 5u){
            ScalarRoundStep<0u>(a, b, c, d, e, words[index + 0u]);
            ScalarRoundStep<0u>(e, a, b, c, d, words[index + 1u]);
            ScalarRoundStep<0u>(d, e, a, b, c, words[index + 2u]);
            ScalarRoundStep<0u>(c, d, e, a, b, words[index + 3u]);
            ScalarRoundStep<0u>(b, c, d, e, a, words[index + 4u]);
        }
        for(u32 index = Sha1Detail::s_RoundPhase1End; index < Sha1Detail::s_RoundPhase2End; index += 5u){
            ScalarRoundStep<1u>(a, b, c, d, e, words[index + 0u]);
            ScalarRoundStep<1u>(e, a, b, c, d, words[index + 1u]);
            ScalarRoundStep<1u>(d, e, a, b, c, words[index + 2u]);
            ScalarRoundStep<1u>(c, d, e, a, b, words[index + 3u]);
            ScalarRoundStep<1u>(b, c, d, e, a, words[index + 4u]);
        }
        for(u32 index = Sha1Detail::s_RoundPhase2End; index < Sha1Detail::s_RoundPhase3End; index += 5u){
            ScalarRoundStep<2u>(a, b, c, d, e, words[index + 0u]);
            ScalarRoundStep<2u>(e, a, b, c, d, words[index + 1u]);
            ScalarRoundStep<2u>(d, e, a, b, c, words[index + 2u]);
            ScalarRoundStep<2u>(c, d, e, a, b, words[index + 3u]);
            ScalarRoundStep<2u>(b, c, d, e, a, words[index + 4u]);
        }
        for(u32 index = Sha1Detail::s_RoundPhase3End; index < Sha1Detail::s_RoundCount; index += 5u){
            ScalarRoundStep<3u>(a, b, c, d, e, words[index + 0u]);
            ScalarRoundStep<3u>(e, a, b, c, d, words[index + 1u]);
            ScalarRoundStep<3u>(d, e, a, b, c, words[index + 2u]);
            ScalarRoundStep<3u>(c, d, e, a, b, words[index + 3u]);
            ScalarRoundStep<3u>(b, c, d, e, a, words[index + 4u]);
        }

        state[0u] += a;
        state[1u] += b;
        state[2u] += c;
        state[3u] += d;
        state[4u] += e;
        blocks += ShaBe32Detail::s_BlockByteCount;
    }
}

template<void (*CompressFn)(Array<u32, Sha1Detail::s_StateWordCount>&, const u8*, usize)>
[[nodiscard]] inline Sha1Digest ComputeSha1WithKernel(const BinaryByteView input)noexcept{
    Array<u32, Sha1Detail::s_StateWordCount> state = Sha1Detail::s_InitialState;
    const usize inputSize = input.size();
    const u8* const inputData = input.data();
    const usize fullBlocks = inputSize / ShaBe32Detail::s_BlockByteCount;
    const usize offset = fullBlocks * ShaBe32Detail::s_BlockByteCount;
    if(fullBlocks > 0u)
        CompressFn(state, inputData, fullBlocks);

    alignas(16) Array<u8, ShaBe32Detail::s_BlockByteCount * 2u> tail;
    const usize tailBlockCount = ShaBe32Detail::PrepareShaBe32TailBlocks(inputData, inputSize, offset, tail);
    CompressFn(state, tail.data(), tailBlockCount);
    return ShaBe32Detail::FinalizeShaBe32Digest<Sha1Digest, Sha1Detail::s_StateWordCount>(state);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Sha1Detail::CompressBlocks(Array<u32, s_StateWordCount>& state, const u8* const blocks, const usize blockCount)noexcept{
    if(blockCount == 0u || !blocks)
        return;

#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
    if(__hidden_sha1::HasArmv8Sha1()){
        __hidden_sha1::CompressBlocksArmv8Sha1(state, blocks, blockCount);
        return;
    }
#elif defined(FOUNDATION_ARCH_X86_64) || defined(__x86_64__) || defined(_M_X64)
    if(__hidden_sha1::HasX86ShaNi()){
        __hidden_sha1::CompressBlocksX86ShaNi(state, blocks, blockCount);
        return;
    }
#endif
    __hidden_sha1::CompressBlocksScalar(state, blocks, blockCount);
}

Sha1Digest Sha1Detail::ComputeSha1Impl(const BinaryByteView input)noexcept{
#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
    if(__hidden_sha1::HasArmv8Sha1())
        return __hidden_sha1::ComputeSha1WithKernel<&__hidden_sha1::CompressBlocksArmv8Sha1>(input);
#elif defined(FOUNDATION_ARCH_X86_64) || defined(__x86_64__) || defined(_M_X64)
    if(__hidden_sha1::HasX86ShaNi())
        return __hidden_sha1::ComputeSha1WithKernel<&__hidden_sha1::CompressBlocksX86ShaNi>(input);
#endif
    return __hidden_sha1::ComputeSha1WithKernel<&__hidden_sha1::CompressBlocksScalar>(input);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

