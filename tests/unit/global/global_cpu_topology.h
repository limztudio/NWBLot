// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/cpu_topology.h>
#include <global/optional.h>
#include <tests/common/gtest_sdk.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

using CpuPlacements = ::InteropVector<CpuWorkerPlacement>;

[[nodiscard]] inline Optional<u32> CurrentThreadGroup(){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    GROUP_AFFINITY affinity{};
    if(GetThreadGroupAffinity(GetCurrentThread(), &affinity))
        return affinity.Group;
#endif
    return {};
}

[[nodiscard]] inline b8 RestrictProcessMask(u64 mask){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(mask)) != 0;
#else
    (void)mask;
    return false;
#endif
}

struct WindowsCpuSet{
    u32 bytes = 0u;
    b8 isCpuSet = false;
    u32 processorGroup = 0u;
    u32 logicalProcessor = 0u;
    u32 id = 0u;
};

class CpuSetBuffer{
public:
    [[nodiscard]] Optional<u32> requiredBytes()const{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        ULONG bytes = 0u;
        if(GetSystemCpuSetInformation(nullptr, 0u, &bytes, GetCurrentProcess(), 0u) || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
            return static_cast<u32>(bytes);
#endif
        return {};
    }

    [[nodiscard]] Optional<u32> read(const u32 capacity){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        m_words.resize((static_cast<usize>(capacity) + sizeof(u64) - 1u) / sizeof(u64));
        ULONG bytes = 0u;
        if(GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(m_words.data()), capacity, &bytes, GetCurrentProcess(), 0u))
            return static_cast<u32>(bytes);
#else
        (void)capacity;
#endif
        return {};
    }

    [[nodiscard]] WindowsCpuSet at(const u32 offset)const{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(reinterpret_cast<const u8*>(m_words.data()) + offset);
        if(info->Type == CpuSetInformation)
            return { info->Size, true, info->CpuSet.Group, info->CpuSet.LogicalProcessorIndex, info->CpuSet.Id };
        return { info->Size };
#else
        (void)offset;
        return {};
#endif
    }

private:
    InteropVector<u64> m_words;
};

[[nodiscard]] inline b8 RestrictDefaultCpuSet(const u32 id){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const ULONG nativeId = id;
    return SetProcessDefaultCpuSets(GetCurrentProcess(), &nativeId, 1u) != 0;
#else
    (void)id;
    return false;
#endif
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

