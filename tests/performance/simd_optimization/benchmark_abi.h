// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/type.h>
#include <global/simdmath.h>


#if defined(FOUNDATION_PLATFORM_LINUX)
#include <fenv.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace SimdBenchmarkAbi{

#if defined(FOUNDATION_PLATFORM_LINUX)
inline constexpr b8 s_HasPosixFpTraps = true;
inline volatile sig_atomic_t s_TrapExitOffset = 0;
inline void FloatTrapExit(int, siginfo_t* information, void*)noexcept{ _exit(s_TrapExitOffset + information->si_code); }
inline b8 InstallFloatTrapExit(const i32 offset)noexcept{
    s_TrapExitOffset = offset;
    struct sigaction action{};
    action.sa_sigaction = FloatTrapExit;
    action.sa_flags = SA_SIGINFO;
    return sigemptyset(&action.sa_mask) == 0 && sigaction(SIGFPE, &action, nullptr) == 0;
}
inline b8 EnableFloatTraps(const i32 mask)noexcept{ return feenableexcept(mask) >= 0; }
#else
inline constexpr b8 s_HasPosixFpTraps = false;
inline b8 InstallFloatTrapExit(i32)noexcept{ return false; }
inline b8 EnableFloatTraps(i32)noexcept{ return false; }
#endif


#if defined(FOUNDATION_HAS_SSE2)
inline constexpr b8 s_HasSse2 = true;
#else
inline constexpr b8 s_HasSse2 = false;
#endif
#if defined(FOUNDATION_HAS_SSE2) && !defined(FOUNDATION_HAS_AVX)
inline constexpr b8 s_UsesSsePairs = true;
#else
inline constexpr b8 s_UsesSsePairs = false;
#endif

[[nodiscard]] inline SIMDVectorDouble DoubleRow(const SIMDMatrixDouble& matrix, const usize index)noexcept{ return matrix.v[index]; }

[[nodiscard]] inline SIMDVectorDouble SseReplicatedReciprocalSqrt(const SIMDVectorDouble value)noexcept{
#if defined(FOUNDATION_HAS_SSE2) && !defined(FOUNDATION_HAS_AVX)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(value);
#else
    return VectorDoubleReciprocalSqrt(value);
#endif
}

#if defined(FOUNDATION_HAS_AVX)
inline constexpr b8 s_HasAvx = true;
#else
inline constexpr b8 s_HasAvx = false;
#endif

[[nodiscard]] inline u32 AvxMoveMask(const SIMDVectorDouble value)noexcept{
#if defined(FOUNDATION_HAS_AVX)
    return static_cast<u32>(_mm256_movemask_pd(BitCast<__m256d>(value)));
#else
    return VectorDoubleMoveMask(value);
#endif
}

[[nodiscard]] inline u32 PairMoveMask(const SIMDVectorDouble value, const b8 high)noexcept{
#if defined(FOUNDATION_HAS_SSE2)
    return static_cast<u32>(_mm_movemask_pd(high ? SIMDVectorDoubleDetail::GetHigh(value) : SIMDVectorDoubleDetail::GetLow(value)));
#else
    return (VectorDoubleMoveMask(value) >> (high ? 2u : 0u)) & 3u;
#endif
}

[[nodiscard]] inline SIMDVectorDouble ShiftDoubleSignLanes(const SIMDVectorDouble value)noexcept{
    return BitCast<SIMDVectorDouble>(BitCast<SIMDVectorDoubleDetail::UIntVector>(value) >> 63u);
}

[[nodiscard]] inline SIMDVectorSingle LowFloatBits(const SIMDVectorDouble value)noexcept{
    return BitCast<SIMDVectorSingle>(__builtin_shufflevector(value, value, 0, 1));
}

inline void ConsumeChecksum(const u64 value)noexcept{
    [[maybe_unused]] static volatile u64 checksum;
    checksum = value;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

