// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "containers.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CpuAffinity{
    enum Enum : u8{
        Any,
        Performance,
        Efficiency,
    };
};


struct CpuWorkerPlacement{
    static constexpr u32 s_InvalidProcessor = Limit<u32>::s_Max;

    u32 logicalProcessorIndex = s_InvalidProcessor;
    u32 processorGroup = 0u;
    // Platform ordinal capacity: a larger value identifies a faster tier. Values are not comparable across machines.
    u32 performanceClass = 0u;
    // Any means homogeneous or unavailable capacity information; Efficiency includes every tier below the fastest.
    CpuAffinity::Enum affinity = CpuAffinity::Any;


    [[nodiscard]] inline bool valid()const noexcept{ return logicalProcessorIndex != s_InvalidProcessor; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Query during initialization on the thread that will create the workers. Honors process CPU sets and inherited affinity.
// Returns false with an empty result when usable processor identities cannot be established; never invents CPU indices.
[[nodiscard]] bool QueryCpuWorkerPlacements(InteropVector<CpuWorkerPlacement>& outPlacements);
// Applies one actual processor identity, including Windows groups and Linux indices beyond 63. Failure leaves OS scheduling active.
[[nodiscard]] bool SetCurrentThreadCpuPlacement(const CpuWorkerPlacement& placement);

// Legacy masks are limited to the calling thread's primary Windows group, or Linux CPU indices 0-63. Any is unpinned (zero).
[[nodiscard]] u64 QueryCpuAffinityMask(CpuAffinity::Enum type);
[[nodiscard]] u32 QueryCpuCoreCount(CpuAffinity::Enum type);
void SetCurrentThreadCpuAffinity(u64 mask);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

