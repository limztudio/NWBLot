// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sha256.h"

#include "platform.h"

#include <array>

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


namespace __hidden_sha256{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
[[nodiscard]] b8 DetectArmv8Sha2()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::IsProcessorFeaturePresent(PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE) != 0;
#elif defined(FOUNDATION_PLATFORM_LINUX)
#if defined(HWCAP_SHA2)
    return (::getauxval(AT_HWCAP) & HWCAP_SHA2) != 0ul;
#else
    return false;
#endif
#else
    return false;
#endif
}

[[nodiscard]] inline b8 HasArmv8Sha2()noexcept{
    static const b8 s_Supported = DetectArmv8Sha2();
    return s_Supported;
}

[[gnu::target("sha2,neon")]] inline void Armv8Sha2RoundQuad(
    uint32x4_t& abcd,
    uint32x4_t& efgh,
    const uint32x4_t msg,
    const u32 roundOffset
)noexcept{
    const uint32x4_t wk = vaddq_u32(msg, vld1q_u32(&Sha256Detail::s_RoundConstants[roundOffset]));
    const uint32x4_t prevAbcd = abcd;
    abcd = vsha256hq_u32(abcd, efgh, wk);
    efgh = vsha256h2q_u32(efgh, prevAbcd, wk);
}

[[gnu::target("sha2,neon")]] inline uint32x4_t Armv8Sha2ScheduleQuad(
    const uint32x4_t m0,
    const uint32x4_t m1,
    const uint32x4_t m2,
    const uint32x4_t m3
)noexcept{
    return vsha256su1q_u32(vsha256su0q_u32(m0, m1), m2, m3);
}

[[gnu::target("sha2,neon")]] void CompressBlocksArmv8Sha2(
    Array<u32, Sha256Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    uint32x4_t abcd = vld1q_u32(state.data());
    uint32x4_t efgh = vld1q_u32(state.data() + 4u);

    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        const uint32x4_t abcdSave = abcd;
        const uint32x4_t efghSave = efgh;

        uint32x4_t msg0 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 0u)));
        uint32x4_t msg1 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 16u)));
        uint32x4_t msg2 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 32u)));
        uint32x4_t msg3 = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(blocks + 48u)));

        Armv8Sha2RoundQuad(abcd, efgh, msg0, 0u);
        msg0 = Armv8Sha2ScheduleQuad(msg0, msg1, msg2, msg3);
        Armv8Sha2RoundQuad(abcd, efgh, msg1, 4u);
        msg1 = Armv8Sha2ScheduleQuad(msg1, msg2, msg3, msg0);
        Armv8Sha2RoundQuad(abcd, efgh, msg2, 8u);
        msg2 = Armv8Sha2ScheduleQuad(msg2, msg3, msg0, msg1);
        Armv8Sha2RoundQuad(abcd, efgh, msg3, 12u);
        msg3 = Armv8Sha2ScheduleQuad(msg3, msg0, msg1, msg2);

        Armv8Sha2RoundQuad(abcd, efgh, msg0, 16u);
        msg0 = Armv8Sha2ScheduleQuad(msg0, msg1, msg2, msg3);
        Armv8Sha2RoundQuad(abcd, efgh, msg1, 20u);
        msg1 = Armv8Sha2ScheduleQuad(msg1, msg2, msg3, msg0);
        Armv8Sha2RoundQuad(abcd, efgh, msg2, 24u);
        msg2 = Armv8Sha2ScheduleQuad(msg2, msg3, msg0, msg1);
        Armv8Sha2RoundQuad(abcd, efgh, msg3, 28u);
        msg3 = Armv8Sha2ScheduleQuad(msg3, msg0, msg1, msg2);

        Armv8Sha2RoundQuad(abcd, efgh, msg0, 32u);
        msg0 = Armv8Sha2ScheduleQuad(msg0, msg1, msg2, msg3);
        Armv8Sha2RoundQuad(abcd, efgh, msg1, 36u);
        msg1 = Armv8Sha2ScheduleQuad(msg1, msg2, msg3, msg0);
        Armv8Sha2RoundQuad(abcd, efgh, msg2, 40u);
        msg2 = Armv8Sha2ScheduleQuad(msg2, msg3, msg0, msg1);
        Armv8Sha2RoundQuad(abcd, efgh, msg3, 44u);
        msg3 = Armv8Sha2ScheduleQuad(msg3, msg0, msg1, msg2);

        Armv8Sha2RoundQuad(abcd, efgh, msg0, 48u);
        Armv8Sha2RoundQuad(abcd, efgh, msg1, 52u);
        Armv8Sha2RoundQuad(abcd, efgh, msg2, 56u);
        Armv8Sha2RoundQuad(abcd, efgh, msg3, 60u);

        abcd = vaddq_u32(abcd, abcdSave);
        efgh = vaddq_u32(efgh, efghSave);
        blocks += ShaBe32Detail::s_BlockByteCount;
    }

    vst1q_u32(state.data(), abcd);
    vst1q_u32(state.data() + 4u, efgh);
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

[[gnu::target("sha,sse4.1")]] inline void X86ShaNiRoundQuad(
    __m128i& abef,
    __m128i& cdgh,
    const __m128i msg,
    const u32 roundOffset
)noexcept{
    const __m128i wk = _mm_add_epi32(msg, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&Sha256Detail::s_RoundConstants[roundOffset])));
    cdgh = _mm_sha256rnds2_epu32(cdgh, abef, wk);
    abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(wk, 0x0E));
}

[[gnu::target("sha,sse4.1")]] inline __m128i X86ShaNiScheduleQuad(
    const __m128i m0,
    const __m128i m1,
    const __m128i m2,
    const __m128i m3
)noexcept{
    const __m128i t0 = _mm_sha256msg1_epu32(m0, m1);
    const __m128i t1 = _mm_alignr_epi8(m3, m2, 4);
    return _mm_sha256msg2_epu32(_mm_add_epi32(t0, t1), m3);
}

[[gnu::target("sha,sse4.1")]] void CompressBlocksX86ShaNi(
    Array<u32, Sha256Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    const __m128i bswapMask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3);
    __m128i abef = _mm_set_epi32(static_cast<int>(state[0u]), static_cast<int>(state[1u]), static_cast<int>(state[4u]), static_cast<int>(state[5u]));
    __m128i cdgh = _mm_set_epi32(static_cast<int>(state[2u]), static_cast<int>(state[3u]), static_cast<int>(state[6u]), static_cast<int>(state[7u]));

    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        const __m128i abefSave = abef;
        const __m128i cdghSave = cdgh;

        __m128i msg0 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 0u)), bswapMask);
        __m128i msg1 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 16u)), bswapMask);
        __m128i msg2 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 32u)), bswapMask);
        __m128i msg3 = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(blocks + 48u)), bswapMask);

        X86ShaNiRoundQuad(abef, cdgh, msg0, 0u);
        msg0 = X86ShaNiScheduleQuad(msg0, msg1, msg2, msg3);
        X86ShaNiRoundQuad(abef, cdgh, msg1, 4u);
        msg1 = X86ShaNiScheduleQuad(msg1, msg2, msg3, msg0);
        X86ShaNiRoundQuad(abef, cdgh, msg2, 8u);
        msg2 = X86ShaNiScheduleQuad(msg2, msg3, msg0, msg1);
        X86ShaNiRoundQuad(abef, cdgh, msg3, 12u);
        msg3 = X86ShaNiScheduleQuad(msg3, msg0, msg1, msg2);

        X86ShaNiRoundQuad(abef, cdgh, msg0, 16u);
        msg0 = X86ShaNiScheduleQuad(msg0, msg1, msg2, msg3);
        X86ShaNiRoundQuad(abef, cdgh, msg1, 20u);
        msg1 = X86ShaNiScheduleQuad(msg1, msg2, msg3, msg0);
        X86ShaNiRoundQuad(abef, cdgh, msg2, 24u);
        msg2 = X86ShaNiScheduleQuad(msg2, msg3, msg0, msg1);
        X86ShaNiRoundQuad(abef, cdgh, msg3, 28u);
        msg3 = X86ShaNiScheduleQuad(msg3, msg0, msg1, msg2);

        X86ShaNiRoundQuad(abef, cdgh, msg0, 32u);
        msg0 = X86ShaNiScheduleQuad(msg0, msg1, msg2, msg3);
        X86ShaNiRoundQuad(abef, cdgh, msg1, 36u);
        msg1 = X86ShaNiScheduleQuad(msg1, msg2, msg3, msg0);
        X86ShaNiRoundQuad(abef, cdgh, msg2, 40u);
        msg2 = X86ShaNiScheduleQuad(msg2, msg3, msg0, msg1);
        X86ShaNiRoundQuad(abef, cdgh, msg3, 44u);
        msg3 = X86ShaNiScheduleQuad(msg3, msg0, msg1, msg2);

        X86ShaNiRoundQuad(abef, cdgh, msg0, 48u);
        X86ShaNiRoundQuad(abef, cdgh, msg1, 52u);
        X86ShaNiRoundQuad(abef, cdgh, msg2, 56u);
        X86ShaNiRoundQuad(abef, cdgh, msg3, 60u);

        abef = _mm_add_epi32(abef, abefSave);
        cdgh = _mm_add_epi32(cdgh, cdghSave);
        blocks += ShaBe32Detail::s_BlockByteCount;
    }

    alignas(16) std::array<u32, 4u> abefOut{};
    alignas(16) std::array<u32, 4u> cdghOut{};
    _mm_store_si128(reinterpret_cast<__m128i*>(abefOut.data()), abef);
    _mm_store_si128(reinterpret_cast<__m128i*>(cdghOut.data()), cdgh);
    state[0u] = abefOut[3u];
    state[1u] = abefOut[2u];
    state[2u] = cdghOut[3u];
    state[3u] = cdghOut[2u];
    state[4u] = abefOut[1u];
    state[5u] = abefOut[0u];
    state[6u] = cdghOut[1u];
    state[7u] = cdghOut[0u];
}
#endif

inline void ScalarRound(
    const u32 a,
    const u32 b,
    const u32 c,
    u32& d,
    const u32 e,
    const u32 f,
    const u32 g,
    u32& h,
    const u32 k,
    const u32 w
)noexcept{
    const u32 s1 = Sha256Detail::Rotate(e, Sha256Detail::s_RoundSigma1RotateA) ^ Sha256Detail::Rotate(e, Sha256Detail::s_RoundSigma1RotateB) ^ Sha256Detail::Rotate(e, Sha256Detail::s_RoundSigma1RotateC);
    const u32 ch = (e & f) ^ (~e & g);
    const u32 temp1 = h + s1 + ch + k + w;
    const u32 s0 = Sha256Detail::Rotate(a, Sha256Detail::s_RoundSigma0RotateA) ^ Sha256Detail::Rotate(a, Sha256Detail::s_RoundSigma0RotateB) ^ Sha256Detail::Rotate(a, Sha256Detail::s_RoundSigma0RotateC);
    const u32 maj = (a & b) ^ (a & c) ^ (b & c);
    d += temp1;
    h = temp1 + s0 + maj;
}

void CompressBlocksScalar(
    Array<u32, Sha256Detail::s_StateWordCount>& state,
    const u8* blocks,
    const usize blockCount
)noexcept{
    for(usize blockIndex = 0u; blockIndex < blockCount; ++blockIndex){
        Array<u32, Sha256Detail::s_RoundCount> words;
        ShaBe32Detail::UnpackBlockWords(blocks, words.data());
        for(u32 index = ShaBe32Detail::s_BlockWordCount; index < Sha256Detail::s_RoundCount; ++index){
            const u32 w0 = words[index - Sha256Detail::s_ScheduleLagSigma0];
            const u32 w1 = words[index - Sha256Detail::s_ScheduleLagSigma1];
            const u32 sigma0 = Sha256Detail::Rotate(w0, Sha256Detail::s_ScheduleSigma0RotateA) ^ Sha256Detail::Rotate(w0, Sha256Detail::s_ScheduleSigma0RotateB) ^ (w0 >> Sha256Detail::s_ScheduleSigma0Shift);
            const u32 sigma1 = Sha256Detail::Rotate(w1, Sha256Detail::s_ScheduleSigma1RotateA) ^ Sha256Detail::Rotate(w1, Sha256Detail::s_ScheduleSigma1RotateB) ^ (w1 >> Sha256Detail::s_ScheduleSigma1Shift);
            words[index] = words[index - Sha256Detail::s_ScheduleLagPrevious] + sigma0 + words[index - Sha256Detail::s_ScheduleLagAdded] + sigma1;
        }

        u32 a = state[0u];
        u32 b = state[1u];
        u32 c = state[2u];
        u32 d = state[3u];
        u32 e = state[4u];
        u32 f = state[5u];
        u32 g = state[6u];
        u32 h = state[7u];
        for(u32 index = 0u; index < Sha256Detail::s_RoundCount; index += 8u){
            ScalarRound(a, b, c, d, e, f, g, h, Sha256Detail::s_RoundConstants[index + 0u], words[index + 0u]);
            ScalarRound(h, a, b, c, d, e, f, g, Sha256Detail::s_RoundConstants[index + 1u], words[index + 1u]);
            ScalarRound(g, h, a, b, c, d, e, f, Sha256Detail::s_RoundConstants[index + 2u], words[index + 2u]);
            ScalarRound(f, g, h, a, b, c, d, e, Sha256Detail::s_RoundConstants[index + 3u], words[index + 3u]);
            ScalarRound(e, f, g, h, a, b, c, d, Sha256Detail::s_RoundConstants[index + 4u], words[index + 4u]);
            ScalarRound(d, e, f, g, h, a, b, c, Sha256Detail::s_RoundConstants[index + 5u], words[index + 5u]);
            ScalarRound(c, d, e, f, g, h, a, b, Sha256Detail::s_RoundConstants[index + 6u], words[index + 6u]);
            ScalarRound(b, c, d, e, f, g, h, a, Sha256Detail::s_RoundConstants[index + 7u], words[index + 7u]);
        }

        state[0u] += a;
        state[1u] += b;
        state[2u] += c;
        state[3u] += d;
        state[4u] += e;
        state[5u] += f;
        state[6u] += g;
        state[7u] += h;
        blocks += ShaBe32Detail::s_BlockByteCount;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Sha256Detail::CompressBlocks(Array<u32, s_StateWordCount>& state, const u8* const blocks, const usize blockCount)noexcept{
    if(blockCount == 0u || !blocks)
        return;

#if defined(FOUNDATION_ARCH_ARM64) || defined(__aarch64__) || defined(_M_ARM64)
    if(__hidden_sha256::HasArmv8Sha2()){
        __hidden_sha256::CompressBlocksArmv8Sha2(state, blocks, blockCount);
        return;
    }
#elif defined(FOUNDATION_ARCH_X86_64) || defined(__x86_64__) || defined(_M_X64)
    if(__hidden_sha256::HasX86ShaNi()){
        __hidden_sha256::CompressBlocksX86ShaNi(state, blocks, blockCount);
        return;
    }
#endif
    __hidden_sha256::CompressBlocksScalar(state, blocks, blockCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

