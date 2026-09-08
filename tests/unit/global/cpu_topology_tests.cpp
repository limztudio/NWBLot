// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/cpu_topology.h>

#include <global/platform.h>
#include <global/simplemath.h>
#include <global/thread.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif
#if defined(NWB_PLATFORM_LINUX)
#include <sched.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTopologyTests, EnumeratesUniqueUsableProcessorsAndAccountsForEveryCapacityTier){
    InteropVector<CpuWorkerPlacement> placements;
    ASSERT_TRUE(QueryCpuWorkerPlacements(placements));
    ASSERT_FALSE(placements.empty());
    u32 minimumClass = Limit<u32>::s_Max;
    u32 maximumClass = 0u;
    for(usize i = 0u; i < placements.size(); ++i){
        const CpuWorkerPlacement& placement = placements[i];
        EXPECT_TRUE(placement.valid());
        minimumClass = Min(minimumClass, placement.performanceClass);
        maximumClass = Max(maximumClass, placement.performanceClass);
        for(usize j = 0u; j < i; ++j){
            EXPECT_FALSE(
                placements[j].processorGroup == placement.processorGroup
                && placements[j].logicalProcessorIndex == placement.logicalProcessorIndex
            );
        }
    }
    for(const CpuWorkerPlacement& placement : placements){
        if(minimumClass == maximumClass)
            EXPECT_EQ(placement.affinity, CpuAffinity::Any);
        else if(placement.performanceClass == maximumClass)
            EXPECT_EQ(placement.affinity, CpuAffinity::Performance);
        else
            EXPECT_EQ(placement.affinity, CpuAffinity::Efficiency);
    }
    EXPECT_EQ(QueryCpuCoreCount(CpuAffinity::Any), placements.size());
    if(minimumClass == maximumClass){
        EXPECT_EQ(QueryCpuCoreCount(CpuAffinity::Performance), placements.size());
        EXPECT_EQ(QueryCpuCoreCount(CpuAffinity::Efficiency), placements.size());
    }
    else{
        EXPECT_EQ(QueryCpuCoreCount(CpuAffinity::Performance) + QueryCpuCoreCount(CpuAffinity::Efficiency), placements.size());
    }
}


TEST(CpuTopologyTests, InvalidPlacementFailsWithoutMutatingTheCallingThread){
    EXPECT_FALSE(SetCurrentThreadCpuPlacement(CpuWorkerPlacement{}));
    EXPECT_FALSE(SetCurrentThreadCpuPlacement(CpuWorkerPlacement{ 0u, Limit<u32>::s_Max, 0u, CpuAffinity::Any }));
}


TEST(CpuTopologyTests, EveryEnumeratedPlacementCanPinAnIndependentWorker){
    InteropVector<CpuWorkerPlacement> placements;
    ASSERT_TRUE(QueryCpuWorkerPlacements(placements));
    for(const CpuWorkerPlacement& placement : placements){
        bool applied = false;
        bool verified = false;
        JoiningThread worker([&](){
            applied = SetCurrentThreadCpuPlacement(placement);
            if(!applied)
                return;
#if defined(NWB_PLATFORM_WINDOWS)
            GROUP_AFFINITY actual{};
            if(!GetThreadGroupAffinity(GetCurrentThread(), &actual))
                return;
            verified = actual.Group == placement.processorGroup && actual.Mask == (static_cast<KAFFINITY>(1u) << placement.logicalProcessorIndex);
#elif defined(NWB_PLATFORM_LINUX)
            const int processor = ::sched_getcpu();
            verified = processor >= 0 && static_cast<u32>(processor) == placement.logicalProcessorIndex;
#endif
        });
        worker.join();
        EXPECT_TRUE(applied) << "group=" << placement.processorGroup << " processor=" << placement.logicalProcessorIndex;
        EXPECT_TRUE(verified);
    }
}


#if defined(NWB_PLATFORM_WINDOWS)
TEST(CpuTopologyTests, DiscoveryHonorsAProcessAffinityRestriction){
    ASSERT_EXIT({
        InteropVector<CpuWorkerPlacement> placements;
        GROUP_AFFINITY original{};
        if(!QueryCpuWorkerPlacements(placements) || !GetThreadGroupAffinity(GetCurrentThread(), &original))
            ExitProcess(1u);
        const auto selected = FindIf(placements.begin(), placements.end(), [&original](const CpuWorkerPlacement& placement){
            return placement.processorGroup == original.Group;
        });
        if(selected == placements.end())
            ExitProcess(2u);
        const u32 processorIndex = selected->logicalProcessorIndex;
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1u) << processorIndex;
        if(!SetProcessAffinityMask(GetCurrentProcess(), mask) || !QueryCpuWorkerPlacements(placements))
            ExitProcess(3u);
        if(
            placements.size() != 1u || placements.front().processorGroup != original.Group
            || placements.front().logicalProcessorIndex != processorIndex || QueryCpuCoreCount(CpuAffinity::Any) != 1u
        )
            ExitProcess(4u);
        ExitProcess(0u);
    }, testing::ExitedWithCode(0), "");
}


TEST(CpuTopologyTests, DiscoveryHonorsProcessDefaultCpuSets){
    ASSERT_EXIT({
        InteropVector<CpuWorkerPlacement> placements;
        if(!QueryCpuWorkerPlacements(placements) || placements.empty())
            ExitProcess(1u);
        const CpuWorkerPlacement selected = placements.back();
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
        if(
            !QueryCpuWorkerPlacements(placements) || placements.size() != 1u
            || placements.front().processorGroup != selected.processorGroup
            || placements.front().logicalProcessorIndex != selected.logicalProcessorIndex
            || QueryCpuCoreCount(CpuAffinity::Any) != 1u
        )
            ExitProcess(6u);
        ExitProcess(0u);
    }, testing::ExitedWithCode(0), "");
}
#endif


#if defined(NWB_PLATFORM_LINUX)
TEST(CpuTopologyTests, DiscoveryHonorsInheritedLinuxAffinityIncludingHighProcessorIndices){
    InteropVector<CpuWorkerPlacement> placements;
    ASSERT_TRUE(QueryCpuWorkerPlacements(placements));
    ASSERT_FALSE(placements.empty());
    const CpuWorkerPlacement placement = placements.back();
    bool applied = false;
    bool queried = false;
    u32 usableCount = 0u;
    InteropVector<CpuWorkerPlacement> restrictedPlacements;
    JoiningThread worker([&](){
        applied = SetCurrentThreadCpuPlacement(placement);
        if(!applied)
            return;
        queried = QueryCpuWorkerPlacements(restrictedPlacements);
        usableCount = QueryCpuCoreCount(CpuAffinity::Any);
    });
    worker.join();
    ASSERT_TRUE(applied);
    ASSERT_TRUE(queried);
    ASSERT_EQ(restrictedPlacements.size(), 1u);
    EXPECT_EQ(restrictedPlacements.front().logicalProcessorIndex, placement.logicalProcessorIndex);
    EXPECT_EQ(usableCount, 1u);
    EXPECT_EQ(restrictedPlacements.front().affinity, CpuAffinity::Any);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

