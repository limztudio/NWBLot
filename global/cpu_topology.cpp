// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_topology.h"

#include "platform.h"
#include "simplemath.h"
#include "thread.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif
#if defined(NWB_PLATFORM_LINUX)
#include <cerrno>
#include <cstdio>
#include <sched.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_AffinityMaskBitCount = sizeof(u64) * 8u;


void classifyPlacements(InteropVector<CpuWorkerPlacement>& placements){
    u32 minimumClass = Limit<u32>::s_Max;
    u32 maximumClass = 0u;
    for(const CpuWorkerPlacement& placement : placements){
        minimumClass = Min(minimumClass, placement.performanceClass);
        maximumClass = Max(maximumClass, placement.performanceClass);
    }
    for(CpuWorkerPlacement& placement : placements){
        if(minimumClass == maximumClass)
            placement.affinity = CpuAffinity::Any;
        else
            placement.affinity = placement.performanceClass == maximumClass ? CpuAffinity::Performance : CpuAffinity::Efficiency;
    }
}


#if defined(NWB_PLATFORM_WINDOWS)
static constexpr u32 s_QueryRetryCount = 4u;


[[nodiscard]] bool queryWindowsPlacements(InteropVector<CpuWorkerPlacement>& placements){
    const HANDLE process = GetCurrentProcess();
    GROUP_AFFINITY primaryAffinity{};
    if(!GetThreadGroupAffinity(GetCurrentThread(), &primaryAffinity))
        return false;

    DWORD_PTR processMask = 0u;
    DWORD_PTR systemMask = 0u;
    if(!GetProcessAffinityMask(process, &processMask, &systemMask))
        return false;

    // Windows 11 reports all default groups here. Earlier Windows versions report the groups assigned to this process.
    USHORT groupCount = GetActiveProcessorGroupCount();
    if(groupCount == 0u)
        return false;
    InteropVector<USHORT> processGroups(groupCount);
    if(!GetProcessGroupAffinity(process, &groupCount, processGroups.data()))
        return false;
    processGroups.resize(groupCount);

    InteropVector<ULONG> defaultSets;
    bool defaultSetsReady = false;
    for(u32 attempt = 0u; attempt < s_QueryRetryCount; ++attempt){
        ULONG requiredCount = 0u;
        if(GetProcessDefaultCpuSets(process, defaultSets.data(), static_cast<ULONG>(defaultSets.size()), &requiredCount)){
            defaultSets.resize(requiredCount);
            defaultSetsReady = true;
            break;
        }
        if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredCount == 0u)
            return false;
        defaultSets.resize(requiredCount);
    }
    if(!defaultSetsReady)
        return false;

    InteropVector<u64> informationStorage;
    ULONG informationBytes = 0u;
    bool informationReady = false;
    for(u32 attempt = 0u; attempt < s_QueryRetryCount; ++attempt){
        ULONG requiredBytes = 0u;
        if(GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(informationStorage.data()),
            informationBytes, &requiredBytes, process, 0u
        )){
            informationBytes = requiredBytes;
            informationReady = true;
            break;
        }
        if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredBytes == 0u)
            return false;
        informationStorage.resize((static_cast<usize>(requiredBytes) + sizeof(u64) - 1u) / sizeof(u64));
        informationBytes = requiredBytes;
    }
    if(!informationReady || informationBytes == 0u)
        return false;

    const auto* bytes = reinterpret_cast<const u8*>(informationStorage.data());
    usize offset = 0u;
    placements.reserve(informationBytes / sizeof(SYSTEM_CPU_SET_INFORMATION));
    while(offset < informationBytes){
        if(informationBytes - offset < sizeof(DWORD) + sizeof(CPU_SET_INFORMATION_TYPE))
            return false;
        const auto* information = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(bytes + offset);
        if(information->Size < sizeof(DWORD) + sizeof(CPU_SET_INFORMATION_TYPE) || information->Size > informationBytes - offset)
            return false;
        if(information->Type == CpuSetInformation){
            if(information->Size < sizeof(SYSTEM_CPU_SET_INFORMATION))
                return false;
            const auto& cpuSet = information->CpuSet;
            const bool permittedGroup = FindIf(
                processGroups.begin(), processGroups.end(),
                [&cpuSet](const USHORT group){ return group == cpuSet.Group; }
            ) != processGroups.end();
            const bool permittedSet = defaultSets.empty() || FindIf(
                defaultSets.begin(), defaultSets.end(),
                [&cpuSet](const ULONG id){ return id == cpuSet.Id; }
            ) != defaultSets.end();
            const bool reservedElsewhere = cpuSet.Allocated && !cpuSet.AllocatedToTargetProcess;
            bool permittedMask = true;
            if(processMask != 0u && systemMask != 0u){
                if(cpuSet.Group == primaryAffinity.Group)
                    permittedMask = (processMask & (static_cast<DWORD_PTR>(1u) << cpuSet.LogicalProcessorIndex)) != 0u;
                else if(processMask != systemMask)
                    permittedMask = false;
            }
            if(permittedGroup && permittedSet && permittedMask && !reservedElsewhere){
                placements.push_back(CpuWorkerPlacement{
                    cpuSet.LogicalProcessorIndex,
                    cpuSet.Group,
                    cpuSet.EfficiencyClass,
                    CpuAffinity::Any
                });
            }
        }
        offset += information->Size;
    }
    return !placements.empty();
}
#endif


#if defined(NWB_PLATFORM_LINUX)
static constexpr usize s_MaxCpuAffinityBytes = 1024u * 1024u;


[[nodiscard]] bool queryLinuxAffinity(InteropVector<usize>& affinityWords){
    usize byteCount = sizeof(cpu_set_t);
    while(byteCount <= s_MaxCpuAffinityBytes){
        affinityWords.assign((byteCount + sizeof(usize) - 1u) / sizeof(usize), 0u);
        const usize storageBytes = affinityWords.size() * sizeof(usize);
        if(::sched_getaffinity(0, storageBytes, reinterpret_cast<cpu_set_t*>(affinityWords.data())) == 0)
            return true;
        if(errno != EINVAL)
            return false;
        byteCount *= 2u;
    }
    return false;
}


[[nodiscard]] u32 queryLinuxCapacity(u32 processorIndex){
    char path[128];
    const int pathLength = snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpu_capacity", processorIndex);
    if(pathLength <= 0 || static_cast<usize>(pathLength) >= sizeof(path))
        return 0u;
    InputFileStream stream(path);
    u32 capacity = 0u;
    if(!(stream >> capacity))
        return 0u;
    return capacity;
}


[[nodiscard]] bool queryLinuxPlacements(InteropVector<CpuWorkerPlacement>& placements){
    InteropVector<usize> affinityWords;
    if(!queryLinuxAffinity(affinityWords))
        return false;
    const usize byteCount = affinityWords.size() * sizeof(usize);
    const auto* affinity = reinterpret_cast<const cpu_set_t*>(affinityWords.data());
    const int processorCount = CPU_COUNT_S(byteCount, affinity);
    if(processorCount <= 0)
        return false;
    placements.reserve(static_cast<usize>(processorCount));
    bool allCapacitiesKnown = true;
    for(usize processorIndex = 0u; processorIndex < byteCount * 8u; ++processorIndex){
        if(!CPU_ISSET_S(processorIndex, byteCount, affinity))
            continue;
        const u32 capacity = queryLinuxCapacity(static_cast<u32>(processorIndex));
        allCapacitiesKnown = allCapacitiesKnown && capacity != 0u;
        placements.push_back(CpuWorkerPlacement{ static_cast<u32>(processorIndex), 0u, capacity, CpuAffinity::Any });
    }
    // Missing capacity is unknown, not an efficiency tier. Do not compare partial capacity data or clock frequencies.
    if(!allCapacitiesKnown){
        for(CpuWorkerPlacement& placement : placements)
            placement.performanceClass = 0u;
    }
    return !placements.empty();
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool QueryCpuWorkerPlacements(InteropVector<CpuWorkerPlacement>& outPlacements){
    outPlacements.clear();
#if defined(NWB_PLATFORM_WINDOWS)
    const bool queried = __hidden_cpu_topology::queryWindowsPlacements(outPlacements);
#elif defined(NWB_PLATFORM_LINUX)
    const bool queried = __hidden_cpu_topology::queryLinuxPlacements(outPlacements);
#else
    const bool queried = false;
#endif
    if(!queried){
        outPlacements.clear();
        return false;
    }
    __hidden_cpu_topology::classifyPlacements(outPlacements);
    return true;
}


bool SetCurrentThreadCpuPlacement(const CpuWorkerPlacement& placement){
    if(!placement.valid())
        return false;
#if defined(NWB_PLATFORM_WINDOWS)
    if(placement.processorGroup >= GetActiveProcessorGroupCount() || placement.logicalProcessorIndex >= sizeof(KAFFINITY) * 8u)
        return false;
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(placement.processorGroup);
    affinity.Mask = static_cast<KAFFINITY>(1u) << placement.logicalProcessorIndex;
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != FALSE;
#elif defined(NWB_PLATFORM_LINUX)
    InteropVector<usize> affinityWords;
    if(placement.processorGroup != 0u || !__hidden_cpu_topology::queryLinuxAffinity(affinityWords))
        return false;
    const usize byteCount = affinityWords.size() * sizeof(usize);
    auto* affinity = reinterpret_cast<cpu_set_t*>(affinityWords.data());
    if(placement.logicalProcessorIndex >= byteCount * 8u || !CPU_ISSET_S(placement.logicalProcessorIndex, byteCount, affinity))
        return false;
    CPU_ZERO_S(byteCount, affinity);
    CPU_SET_S(placement.logicalProcessorIndex, byteCount, affinity);
    return ::sched_setaffinity(0, byteCount, affinity) == 0;
#else
    return false;
#endif
}


u64 QueryCpuAffinityMask(CpuAffinity::Enum type){
    if(type == CpuAffinity::Any)
        return 0u;
    InteropVector<CpuWorkerPlacement> placements;
    if(!QueryCpuWorkerPlacements(placements))
        return 0u;
    u32 primaryGroup = 0u;
#if defined(NWB_PLATFORM_WINDOWS)
    GROUP_AFFINITY affinity{};
    if(!GetThreadGroupAffinity(GetCurrentThread(), &affinity))
        return 0u;
    primaryGroup = affinity.Group;
#endif
    u64 mask = 0u;
    for(const CpuWorkerPlacement& placement : placements){
        if(
            placement.affinity == type && placement.processorGroup == primaryGroup
            && placement.logicalProcessorIndex < __hidden_cpu_topology::s_AffinityMaskBitCount
        )
            mask |= 1ULL << placement.logicalProcessorIndex;
    }
    return mask;
}


u32 QueryCpuCoreCount(CpuAffinity::Enum type){
    InteropVector<CpuWorkerPlacement> placements;
    if(!QueryCpuWorkerPlacements(placements))
        return Max(1u, static_cast<u32>(Thread::hardware_concurrency()));
    u32 count = 0u;
    for(const CpuWorkerPlacement& placement : placements){
        if(type == CpuAffinity::Any || placement.affinity == type || placement.affinity == CpuAffinity::Any)
            ++count;
    }
    return count;
}


void SetCurrentThreadCpuAffinity(u64 mask){
    if(mask == 0u)
        return;
#if defined(NWB_PLATFORM_WINDOWS)
    if(SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(mask)) == 0u)
        return;
#elif defined(NWB_PLATFORM_LINUX)
    InteropVector<usize> affinityWords;
    if(!__hidden_cpu_topology::queryLinuxAffinity(affinityWords))
        return;
    const usize byteCount = affinityWords.size() * sizeof(usize);
    auto* affinity = reinterpret_cast<cpu_set_t*>(affinityWords.data());
    for(usize processorIndex = 0u; processorIndex < byteCount * 8u; ++processorIndex){
        if(processorIndex >= __hidden_cpu_topology::s_AffinityMaskBitCount || (mask & (1ULL << processorIndex)) == 0u)
            CPU_CLR_S(processorIndex, byteCount, affinity);
    }
    if(::sched_setaffinity(0, byteCount, affinity) != 0)
        return;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

