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
    // Platform ordinal capacity; larger is faster, not comparable across machines.
    u32 performanceClass = 0u;
    // Any means homogeneous or unknown; Efficiency includes every tier below fastest.
    CpuAffinity::Enum affinity = CpuAffinity::Any;


    [[nodiscard]] inline bool valid()const noexcept{ return logicalProcessorIndex != s_InvalidProcessor; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Query on the worker-creating thread; honors CPU sets; never invents indices.
[[nodiscard]] bool QueryCpuWorkerPlacements(InteropVector<CpuWorkerPlacement>& outPlacements);
// Applies one processor identity, including groups and indices beyond 63.
[[nodiscard]] bool SetCurrentThreadCpuPlacement(const CpuWorkerPlacement& placement);

[[nodiscard]] u32 QueryCpuCoreCount(CpuAffinity::Enum type);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

