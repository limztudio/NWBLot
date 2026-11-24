// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/compile.h>
#include <global/type_properties.h>

#include <tests/performance/simd_optimization/memory_probe_kernels.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimdBenchmarkAbi{
template<typename Batch, typename... Args>
[[gnu::flatten]] FOUNDATION_NOINLINE auto RunMemoryBatch(const Batch& batch, Args&&... args)noexcept{
    return batch.run(Forward<Args>(args)...);
}

template<usize FixedCount>
FOUNDATION_INLINE void InlineCandidateCopy(const NWB::Tests::ReadAddress source, const usize count, const usize capacity, const NWB::Tests::ByteAddress destination)noexcept{
    [[clang::always_inline]] SimdOptimizationBenchmark::CandidateCopy<FixedCount>(source, count, capacity, destination);
}

inline void AccumulateMemoryChecksum(const u64 value)noexcept{
    static volatile u64 s_Checksum = 0u;
    s_Checksum = s_Checksum + value;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

