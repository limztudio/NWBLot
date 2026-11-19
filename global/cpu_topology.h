// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
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
    u32 performanceClass = 0u;
    CpuAffinity::Enum affinity = CpuAffinity::Any;


public:
    [[nodiscard]] inline b8 valid()const noexcept{ return logicalProcessorIndex != s_InvalidProcessor; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<InteropVector<CpuWorkerPlacement>> QueryCpuWorkerPlacements();
[[nodiscard]] b8 SetCurrentThreadCpuPlacement(const CpuWorkerPlacement& placement);

[[nodiscard]] u32 QueryCpuCoreCount(CpuAffinity::Enum type);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

