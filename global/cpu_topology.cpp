// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_topology.h"

#include "containers.h"
#include "limit.h"
#include "platform.h"
#include "sync.h"
#include "thread.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif
#if defined(NWB_PLATFORM_LINUX)
#include <sched.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
static constexpr u8 s_MaxEfficiencyClass = Limit<u8>::s_Max;
static constexpr u32 s_AffinityMaskBitCount = sizeof(u64) * 8u;
#endif


struct AffinityMasks{
    u64 m_performance = 0;
    u64 m_efficiency = 0;

    void initialize(){
#if defined(NWB_PLATFORM_WINDOWS)
        queryMask(m_performance, CpuAffinity::Performance);
        queryMask(m_efficiency, CpuAffinity::Efficiency);
#endif
    }

#if defined(NWB_PLATFORM_WINDOWS)
    void queryMask(u64& outMask, CpuAffinity::Enum type){
        ULONG bufferSize = 0;
        GetSystemCpuSetInformation(nullptr, 0, &bufferSize, GetCurrentProcess(), 0);
        if(bufferSize == 0)
            return;

        InteropVector<u8> buffer(static_cast<usize>(bufferSize));
        if(!GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
            bufferSize, &bufferSize, GetCurrentProcess(), 0
        ))
            return;

        u8 minEfficiency = s_MaxEfficiencyClass;
        u8 maxEfficiency = 0;

        auto* ptr = buffer.data();
        auto* end = ptr + bufferSize;
        while(ptr < end){
            auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(ptr);
            if(info->Type == CpuSetInformation){
                u8 eff = info->CpuSet.EfficiencyClass;
                if(eff < minEfficiency)
                    minEfficiency = eff;
                if(eff > maxEfficiency)
                    maxEfficiency = eff;
            }
            ptr += info->Size;
        }

        if(minEfficiency == maxEfficiency)
            return;

        const u8 targetClass = (type == CpuAffinity::Performance) ? maxEfficiency : minEfficiency;

        u64 mask = 0;
        ptr = buffer.data();
        while(ptr < end){
            auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(ptr);
            if(info->Type == CpuSetInformation && info->CpuSet.EfficiencyClass == targetClass){
                const u32 logicalIndex = info->CpuSet.LogicalProcessorIndex;
                if(logicalIndex < s_AffinityMaskBitCount)
                    mask |= (1ULL << logicalIndex);
            }
            ptr += info->Size;
        }

        outMask = mask;
    }
#endif
};

static AffinityMasks s_AffinityMasks;
static OnceFlag s_AffinityMasksOnce;

AffinityMasks& GetAffinityMasks(){
    CallOnce(s_AffinityMasksOnce, [](){
        s_AffinityMasks.initialize();
    });
    return s_AffinityMasks;
}


u32 QueryCurrentThreadCpuCoreCount(){
#if defined(NWB_PLATFORM_LINUX)
    cpu_set_t cpuSet;
    CPU_ZERO(&cpuSet);
    if(::sched_getaffinity(0, sizeof(cpuSet), &cpuSet) == 0){
        const int coreCount = CPU_COUNT(&cpuSet);
        if(coreCount > 0)
            return static_cast<u32>(coreCount);
    }
#endif
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 QueryCpuAffinityMask(CpuAffinity::Enum type){
    const auto& affinityMasks = __hidden_cpu_topology::GetAffinityMasks();
    switch(type){
    case CpuAffinity::Performance: return affinityMasks.m_performance;
    case CpuAffinity::Efficiency: return affinityMasks.m_efficiency;
    default: return 0;
    }
}

u32 QueryCpuCoreCount(CpuAffinity::Enum type){
    u64 mask = QueryCpuAffinityMask(type);
    if(mask == 0){
        // Linux callers inherit their effective cpuset/taskset mask. Honor it when no platform-specific
        // performance/efficiency mask is available so worker pools do not oversubscribe constrained processes.
        if(type == CpuAffinity::Any){
            const u32 currentThreadCoreCount = __hidden_cpu_topology::QueryCurrentThreadCpuCoreCount();
            if(currentThreadCoreCount > 0u)
                return currentThreadCoreCount;
        }
        return static_cast<u32>(Thread::hardware_concurrency());
    }

    u32 count = 0;
    while(mask){
        count += static_cast<u32>(mask & 1);
        mask >>= 1;
    }
    return count;
}

void SetCurrentThreadCpuAffinity(u64 mask){
#if defined(NWB_PLATFORM_WINDOWS)
    if(mask != 0)
        SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask));
#else
    static_cast<void>(mask);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

