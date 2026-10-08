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


static constexpr usize s_BitsPerByte = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ClassifyPlacements(InteropVector<CpuWorkerPlacement>& placements){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_QueryRetryCount = 4u;


[[nodiscard]] Expected<InteropVector<CpuWorkerPlacement>> QueryWindowsPlacements(){
    InteropVector<CpuWorkerPlacement> placements;
    const HANDLE process = GetCurrentProcess();
    GROUP_AFFINITY primaryAffinity{};
    if(!GetThreadGroupAffinity(GetCurrentThread(), &primaryAffinity))
        return MakeUnexpected(Failure{});

    DWORD_PTR processMask = 0u;
    DWORD_PTR systemMask = 0u;
    if(!GetProcessAffinityMask(process, &processMask, &systemMask))
        return MakeUnexpected(Failure{});

    // Windows 11 reports all default groups here. Earlier Windows versions report the groups assigned to this process.
    USHORT groupCount = GetActiveProcessorGroupCount();
    if(groupCount == 0u)
        return MakeUnexpected(Failure{});
    InteropVector<USHORT> processGroups(groupCount);
    if(!GetProcessGroupAffinity(process, &groupCount, processGroups.data()))
        return MakeUnexpected(Failure{});
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
            return MakeUnexpected(Failure{});
        defaultSets.resize(requiredCount);
    }
    if(!defaultSetsReady)
        return MakeUnexpected(Failure{});

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
            return MakeUnexpected(Failure{});
        informationStorage.resize((static_cast<usize>(requiredBytes) + sizeof(u64) - 1u) / sizeof(u64));
        informationBytes = requiredBytes;
    }
    if(!informationReady || informationBytes == 0u)
        return MakeUnexpected(Failure{});

    const auto* bytes = reinterpret_cast<const u8*>(informationStorage.data());
    usize offset = 0u;
    placements.reserve(informationBytes / sizeof(SYSTEM_CPU_SET_INFORMATION));
    while(offset < informationBytes){
        if(informationBytes - offset < sizeof(DWORD) + sizeof(CPU_SET_INFORMATION_TYPE))
            return MakeUnexpected(Failure{});
        const auto* information = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(bytes + offset);
        if(information->Size < sizeof(DWORD) + sizeof(CPU_SET_INFORMATION_TYPE) || information->Size > informationBytes - offset)
            return MakeUnexpected(Failure{});
        if(information->Type == CpuSetInformation){
            if(information->Size < sizeof(SYSTEM_CPU_SET_INFORMATION))
                return MakeUnexpected(Failure{});
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
    if(placements.empty())
        return MakeUnexpected(Failure{});
    return placements;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_SysfsCapacityPathCapacity = 128u;
static constexpr usize s_MaxCpuAffinityBytes = 1024u * 1024u;


[[nodiscard]] Expected<InteropVector<usize>> QueryLinuxAffinity(){
    InteropVector<usize> affinityWords;
    usize byteCount = sizeof(cpu_set_t);
    while(byteCount <= s_MaxCpuAffinityBytes){
        affinityWords.assign((byteCount + sizeof(usize) - 1u) / sizeof(usize), 0u);
        const usize storageBytes = affinityWords.size() * sizeof(usize);
        if(::sched_getaffinity(0, storageBytes, reinterpret_cast<cpu_set_t*>(affinityWords.data())) == 0)
            return affinityWords;
        if(errno != EINVAL)
            return MakeUnexpected(Failure{});
        byteCount *= 2u;
    }
    return MakeUnexpected(Failure{});
}


[[nodiscard]] u32 QueryLinuxCapacity(u32 processorIndex){
    char path[s_SysfsCapacityPathCapacity];
    const int pathLength = NWB_SPRINTF(path, sizeof(path), "/sys/devices/system/cpu/cpu%u/cpu_capacity", processorIndex);
    if(pathLength <= 0 || static_cast<usize>(pathLength) >= sizeof(path))
        return 0u;
    InputFileStream stream(path);
    u32 capacity = 0u;
    if(!(stream >> capacity))
        return 0u;
    return capacity;
}


[[nodiscard]] Expected<InteropVector<CpuWorkerPlacement>> QueryLinuxPlacements(){
    InteropVector<CpuWorkerPlacement> placements;
    const auto affinityWords = QueryLinuxAffinity();
    if(!affinityWords)
        return MakeUnexpected(affinityWords.error());
    const usize byteCount = affinityWords->size() * sizeof(usize);
    const auto* affinity = reinterpret_cast<const cpu_set_t*>(affinityWords->data());
    const int processorCount = CPU_COUNT_S(byteCount, affinity);
    if(processorCount <= 0)
        return MakeUnexpected(Failure{});
    placements.reserve(static_cast<usize>(processorCount));
    bool allCapacitiesKnown = true;
    for(usize processorIndex = 0u; processorIndex < byteCount * __hidden_cpu_topology::s_BitsPerByte; ++processorIndex){
        if(!CPU_ISSET_S(processorIndex, byteCount, affinity))
            continue;
        const u32 capacity = QueryLinuxCapacity(static_cast<u32>(processorIndex));
        allCapacitiesKnown = allCapacitiesKnown && capacity != 0u;
        placements.push_back(CpuWorkerPlacement{ static_cast<u32>(processorIndex), 0u, capacity, CpuAffinity::Any });
    }
    // Missing capacity is unknown, not an efficiency tier. Do not compare partial capacity data or clock frequencies.
    if(!allCapacitiesKnown){
        for(CpuWorkerPlacement& placement : placements)
            placement.performanceClass = 0u;
    }
    if(placements.empty())
        return MakeUnexpected(Failure{});
    return placements;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<InteropVector<CpuWorkerPlacement>> QueryCpuWorkerPlacements(){
#if defined(NWB_PLATFORM_WINDOWS)
    auto placements = __hidden_cpu_topology::QueryWindowsPlacements();
#elif defined(NWB_PLATFORM_LINUX)
    auto placements = __hidden_cpu_topology::QueryLinuxPlacements();
#else
    return MakeUnexpected(Failure{});
#endif
#if defined(NWB_PLATFORM_WINDOWS) || defined(NWB_PLATFORM_LINUX)
    if(!placements)
        return MakeUnexpected(placements.error());
    __hidden_cpu_topology::ClassifyPlacements(*placements);
    return placements;
#endif
}


bool SetCurrentThreadCpuPlacement(const CpuWorkerPlacement& placement){
    if(!placement.valid())
        return false;
#if defined(NWB_PLATFORM_WINDOWS)
    if(placement.processorGroup >= GetActiveProcessorGroupCount() || placement.logicalProcessorIndex >= sizeof(KAFFINITY) * __hidden_cpu_topology::s_BitsPerByte)
        return false;
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(placement.processorGroup);
    affinity.Mask = static_cast<KAFFINITY>(1u) << placement.logicalProcessorIndex;
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != FALSE;
#elif defined(NWB_PLATFORM_LINUX)
    if(placement.processorGroup != 0u)
        return false;
    auto affinityWords = __hidden_cpu_topology::QueryLinuxAffinity();
    if(!affinityWords)
        return false;
    const usize byteCount = affinityWords->size() * sizeof(usize);
    auto* affinity = reinterpret_cast<cpu_set_t*>(affinityWords->data());
    if(placement.logicalProcessorIndex >= byteCount * __hidden_cpu_topology::s_BitsPerByte || !CPU_ISSET_S(placement.logicalProcessorIndex, byteCount, affinity))
        return false;
    CPU_ZERO_S(byteCount, affinity);
    CPU_SET_S(placement.logicalProcessorIndex, byteCount, affinity);
    return ::sched_setaffinity(0, byteCount, affinity) == 0;
#else
    return false;
#endif
}


u32 QueryCpuCoreCount(CpuAffinity::Enum type){
    const auto placements = QueryCpuWorkerPlacements();
    if(!placements)
        return Max(1u, static_cast<u32>(Thread::hardware_concurrency()));
    u32 count = 0u;
    for(const CpuWorkerPlacement& placement : *placements){
        if(type == CpuAffinity::Any || placement.affinity == type || placement.affinity == CpuAffinity::Any)
            ++count;
    }
    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

