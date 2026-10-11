// limztudio@gmail.com
#pragma once

#include <global/simdmath.h>

namespace NWB::TexConv::AlphaBatchDetail{

[[nodiscard]] inline bool TrapsMasked()noexcept{
#if defined(NWB_HAS_SSE4)
    return (_mm_getcsr() & 0x1f80u) == 0x1f80u;
#elif defined(NWB_HAS_NEON) && (defined(__aarch64__) || defined(_M_ARM64))
    return (__builtin_arm_rsr64("fpcr") & 0x9f00u) == 0u;
#else
    return false;
#endif
}

[[nodiscard]] inline bool Admit(const f32* values, const u32 count)noexcept{
    if constexpr(!IsSame_V<f32, float> || sizeof(f32) != sizeof(u32))
        return false;
    if(count < 2u || count > 4u || !TrapsMasked())
        return false;
    // Integer classification leaves signaling NaNs and rejected packets' FP
    // flags untouched. The caller keeps its original ordered scalar path.
    for(u32 lane = 0u; lane < count; ++lane){
        if((BitCast<u32>(values[lane]) & 0x7fffffffu) >= 0x7f800000u)
            return false;
    }
    return true;
}

[[nodiscard]] inline SIMDVector Load(const f32* values, const u32 count)noexcept{
    // Repeat a real lane for padding; never read a missing pixel.
    return VectorSet(values[0], values[1], count > 2u ? values[2] : values[0],
        count > 3u ? values[3] : values[0]);
}

[[nodiscard]] inline bool TrySaturate(const f32* values, const u32 count, f32* output)noexcept{
    if(!Admit(values, count))
        return false;
    const SIMDVector saturated = VectorSaturate(Load(values, count));
    for(u32 lane = 0u; lane < count; ++lane)
        output[lane] = VectorGetByIndex(saturated, lane);
    return true;
}

[[nodiscard]] inline bool TryQuantize(const f32* values, const u32 count, u8* output)noexcept{
    if(!Admit(values, count))
        return false;
    // Retain the original separate saturation, multiplication, addition and
    // truncation operations, including their existing backend rounding policy.
    const SIMDVector quantized = VectorTruncate(VectorAdd(
        VectorMultiply(VectorSaturate(Load(values, count)), VectorReplicate(255.0f)),
        VectorReplicate(0.5f)));
    for(u32 lane = 0u; lane < count; ++lane)
        output[lane] = static_cast<u8>(VectorGetByIndex(quantized, lane));
    return true;
}

}
