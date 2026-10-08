// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/cpu_topology.h>
#include <global/platform.h>
#include <global/thread.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif
#if defined(NWB_PLATFORM_LINUX)
#include <sched.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTopologyTests, InvalidPlacementFailsWithoutMutatingTheCallingThread){
    EXPECT_FALSE(SetCurrentThreadCpuPlacement(CpuWorkerPlacement{}));
    EXPECT_FALSE(SetCurrentThreadCpuPlacement(CpuWorkerPlacement{ 0u, Limit<u32>::s_Max, 0u, CpuAffinity::Any }));
}


#if defined(NWB_PLATFORM_WINDOWS)
TEST(CpuTopologyTests, DiscoveryHonorsAProcessAffinityRestriction){
    ASSERT_EXIT({
        auto placements = QueryCpuWorkerPlacements();
        GROUP_AFFINITY original{};
        if(!placements || !GetThreadGroupAffinity(GetCurrentThread(), &original))
            ExitProcess(1u);
        const auto selected = FindIf(placements->begin(), placements->end(), [&original](const CpuWorkerPlacement& placement){
            return placement.processorGroup == original.Group;
        });
        if(selected == placements->end())
            ExitProcess(2u);
        const u32 processorIndex = selected->logicalProcessorIndex;
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1u) << processorIndex;
        if(!SetProcessAffinityMask(GetCurrentProcess(), mask))
            ExitProcess(3u);
        placements = QueryCpuWorkerPlacements();
        if(!placements)
            ExitProcess(3u);
        if(
            placements->size() != 1u || placements->front().processorGroup != original.Group
            || placements->front().logicalProcessorIndex != processorIndex || QueryCpuCoreCount(CpuAffinity::Any) != 1u
        )
            ExitProcess(4u);
        ExitProcess(0u);
    }, testing::ExitedWithCode(0), "");
}


TEST(CpuTopologyTests, DiscoveryHonorsProcessDefaultCpuSets){
    ASSERT_EXIT({
        auto placements = QueryCpuWorkerPlacements();
        if(!placements || placements->empty())
            ExitProcess(1u);
        const CpuWorkerPlacement selected = placements->back();
        ULONG byteCount = 0u;
        if(
            !GetSystemCpuSetInformation(nullptr, 0u, &byteCount, GetCurrentProcess(), 0u)
            && GetLastError() != ERROR_INSUFFICIENT_BUFFER
        )
            ExitProcess(2u);
        if(byteCount == 0u)
            ExitProcess(3u);
        InteropVector<u64> storage((static_cast<usize>(byteCount) + sizeof(u64) - 1u) / sizeof(u64));
        auto* information = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(storage.data());
        if(!GetSystemCpuSetInformation(information, byteCount, &byteCount, GetCurrentProcess(), 0u))
            ExitProcess(4u);
        ULONG selectedId = Limit<ULONG>::s_Max;
        for(usize offset = 0u; offset < byteCount; offset += information->Size){
            information = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(reinterpret_cast<u8*>(storage.data()) + offset);
            if(
                information->Type == CpuSetInformation && information->CpuSet.Group == selected.processorGroup
                && information->CpuSet.LogicalProcessorIndex == selected.logicalProcessorIndex
            ){
                selectedId = information->CpuSet.Id;
                break;
            }
        }
        if(selectedId == Limit<ULONG>::s_Max || !SetProcessDefaultCpuSets(GetCurrentProcess(), &selectedId, 1u))
            ExitProcess(5u);
        placements = QueryCpuWorkerPlacements();
        if(
            !placements || placements->size() != 1u
            || placements->front().processorGroup != selected.processorGroup
            || placements->front().logicalProcessorIndex != selected.logicalProcessorIndex
            || QueryCpuCoreCount(CpuAffinity::Any) != 1u
        )
            ExitProcess(6u);
        ExitProcess(0u);
    }, testing::ExitedWithCode(0), "");
}
#endif


#if defined(NWB_PLATFORM_LINUX)
TEST(CpuTopologyTests, DiscoveryHonorsInheritedLinuxAffinityIncludingHighProcessorIndices){
    auto placements = QueryCpuWorkerPlacements();
    ASSERT_TRUE(placements);
    ASSERT_FALSE(placements->empty());
    const CpuWorkerPlacement placement = placements->back();
    bool applied = false;
    u32 usableCount = 0u;
    Expected<InteropVector<CpuWorkerPlacement>> restrictedPlacements = MakeUnexpected(Failure{});
    JoiningThread worker([&](){
        applied = SetCurrentThreadCpuPlacement(placement);
        if(!applied)
            return;
        restrictedPlacements = QueryCpuWorkerPlacements();
        usableCount = QueryCpuCoreCount(CpuAffinity::Any);
    });
    worker.join();
    ASSERT_TRUE(applied);
    ASSERT_TRUE(restrictedPlacements);
    ASSERT_EQ(restrictedPlacements->size(), 1u);
    EXPECT_EQ(restrictedPlacements->front().logicalProcessorIndex, placement.logicalProcessorIndex);
    EXPECT_EQ(usableCount, 1u);
    EXPECT_EQ(restrictedPlacements->front().affinity, CpuAffinity::Any);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

