// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "global.h"

#include <global/math/vector_double.h>

NWB_PERF_BEGIN

// Replicated lanes retain each sequential binary64 addition and its original rounding order.
[[nodiscard]] NWB_INLINE f64 AddTimingSeconds(const f64 total, const f64 sample)noexcept{
    const SIMDVectorDouble accumulated = SIMDVectorDouble{ total, total } + SIMDVectorDouble{ sample, sample };
    return accumulated.x;
}

NWB_PERF_END
