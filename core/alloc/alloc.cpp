// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "alloc.h"
#include <core/common/common.h>

#include <global/thread.h>

#include <cstddef>
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_APPLE)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_alloc{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CacheSize : Core::Common::Initializerable{
    bool initialize()override{
#if defined(_WIN32)
        DWORD bufferSize = 0;
        GetLogicalProcessorInformation(nullptr, &bufferSize);

        ScratchArena<> scratchArena(bufferSize);
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(scratchArena.allocate<u8>(bufferSize));

        if(GetLogicalProcessorInformation(info, &bufferSize)){
            for(DWORD i = 0, e = bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i < e; ++i){
                if(info[i].Relationship != RelationCache)
                    continue;

                const auto cur = static_cast<usize>(info[i].Cache.LineSize);
                if(size < cur)
                    size = cur;
            }
        }
#elif defined(_SC_LEVEL1_DCACHE_LINESIZE)
        auto lineSizes[] = {
            sysconf(_SC_LEVEL1_DCACHE_LINESIZE),
            sysconf(_SC_LEVEL2_DCACHE_LINESIZE),
            sysconf(_SC_LEVEL3_DCACHE_LINESIZE),
        };
        for(auto lineSize : lineSizes){
            if(lineSize <= 0)
                continue;

            const auto cur = static_cast<usize>(lineSize);
            if(size < cur)
                size = cur;
        }
#endif
        return true;
    }
    void finalize()override{}

    usize size = 64;
} static s_CacheSize;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AffinityMasks : Core::Common::Initializerable{
    bool initialize()override{
#if defined(NWB_PLATFORM_WINDOWS)
        queryMask(performance, CoreAffinity::Performance);
        queryMask(efficiency, CoreAffinity::Efficiency);
#endif
        return true;
    }
    void finalize()override{}

#if defined(NWB_PLATFORM_WINDOWS)
    void queryMask(u64& outMask, CoreAffinity type){
        ULONG bufferSize = 0;
        GetSystemCpuSetInformation(nullptr, 0, &bufferSize, GetCurrentProcess(), 0);
        if(bufferSize == 0)
            return;

        ScratchArena<> scratchArena(bufferSize);
        Vector<u8, ScratchAllocator<u8>> buffer(
            bufferSize, ScratchAllocator<u8>(scratchArena)
        );
        if(!GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),
            bufferSize, &bufferSize, GetCurrentProcess(), 0))
            return;

        u8 minEfficiency = 255;
        u8 maxEfficiency = 0;

        auto* ptr = buffer.data();
        auto* end = ptr + bufferSize;
        while(ptr < end){
            auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(ptr);
            if(info->Type == CpuSetInformation){
                u8 eff = info->CpuSet.EfficiencyClass;
                if(eff < minEfficiency) minEfficiency = eff;
                if(eff > maxEfficiency) maxEfficiency = eff;
            }
            ptr += info->Size;
        }

        if(minEfficiency == maxEfficiency)
            return; // homogeneous CPU, no distinction

        u8 targetClass = (type == CoreAffinity::Performance) ? maxEfficiency : minEfficiency;

        u64 mask = 0;
        ptr = buffer.data();
        while(ptr < end){
            auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(ptr);
            if(info->Type == CpuSetInformation && info->CpuSet.EfficiencyClass == targetClass){
                u32 logicalIndex = info->CpuSet.LogicalProcessorIndex;
                if(logicalIndex < 64)
                    mask |= (1ULL << logicalIndex);
            }
            ptr += info->Size;
        }

        outMask = mask;
    }
#endif

    u64 performance = 0;
    u64 efficiency = 0;
} static s_AffinityMasks;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CachelineSize(){ return __hidden_alloc::s_CacheSize.size; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 QueryAffinityMask(CoreAffinity type){
    switch(type){
    case CoreAffinity::Performance: return __hidden_alloc::s_AffinityMasks.performance;
    case CoreAffinity::Efficiency: return __hidden_alloc::s_AffinityMasks.efficiency;
    default: return 0;
    }
}

u32 QueryCoreCount(CoreAffinity type){
    u64 mask = QueryAffinityMask(type);
    if(mask == 0)
        return static_cast<u32>(Thread::hardware_concurrency());

    u32 count = 0;
    while(mask){
        count += static_cast<u32>(mask & 1);
        mask >>= 1;
    }
    return count;
}

void SetCurrentThreadAffinity(u64 mask){
#if defined(NWB_PLATFORM_WINDOWS)
    if(mask != 0)
        SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask));
#else
    (void)mask;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

