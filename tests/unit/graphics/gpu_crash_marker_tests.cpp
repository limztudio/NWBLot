// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/api.h>
#include <core/graphics/vulkan/backend.h>

#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_crash_marker_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuCrashMarkerTestsTag>;


static_assert(IsNothrowDestructible_V<Core::GraphicsBackend::CommandList>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCrashMarkerTracker, SharedDeviceTrackerPreservesHistoryAcrossCommandListTrackerReset){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    Core::GpuCrashMarkerTracker firstCommandListTracker(crashTracker, testArena.arena);
    Core::GpuCrashMarkerTracker secondCommandListTracker(crashTracker, testArena.arena);

    const usize outerHash = firstCommandListTracker.pushEvent("Outer");
    const usize nestedHash = firstCommandListTracker.pushEvent("Inner");
    const Core::ResolvedMarker nested = crashTracker.resolveMarker(nestedHash);
    ASSERT_TRUE(nested.first());
    EXPECT_EQ(nested.second(), AStringView("Outer/Inner"));

    firstCommandListTracker.resetEventStack();

    const usize freshHash = secondCommandListTracker.pushEvent("Fresh");
    const Core::ResolvedMarker fresh = crashTracker.resolveMarker(freshHash);
    ASSERT_TRUE(fresh.first());
    EXPECT_EQ(fresh.second(), AStringView("Fresh"));

    const Core::ResolvedMarker historicalOuter = crashTracker.resolveMarker(outerHash);
    const Core::ResolvedMarker historicalNested = crashTracker.resolveMarker(nestedHash);
    ASSERT_TRUE(historicalOuter.first());
    ASSERT_TRUE(historicalNested.first());
    EXPECT_EQ(historicalOuter.second(), AStringView("Outer"));
    EXPECT_EQ(historicalNested.second(), AStringView("Outer/Inner"));

    secondCommandListTracker.popEvent();
    const usize finalHash = firstCommandListTracker.pushEvent("Final");
    const Core::ResolvedMarker final = crashTracker.resolveMarker(finalHash);
    ASSERT_TRUE(final.first());
    EXPECT_EQ(final.second(), AStringView("Final"));
}


TEST(GpuCrashMarkerTracker, DestroyedCommandListTrackersLeaveAllDeviceHistoryResolvable){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    constexpr AStringView markerNames[] = {
        "Destroyed command list 0",
        "Destroyed command list 1",
        "Destroyed command list 2",
        "Destroyed command list 3",
        "Destroyed command list 4",
        "Destroyed command list 5",
    };
    usize markerHashes[6] = {};

    usize markerIndex = 0u;
    for(const AStringView markerName : markerNames){
        Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
        markerHashes[markerIndex] = commandListTracker.pushEvent(markerName.data());
        ++markerIndex;
    }

    markerIndex = 0u;
    for(const AStringView markerName : markerNames){
        const Core::ResolvedMarker resolved = crashTracker.resolveMarker(markerHashes[markerIndex]);
        ASSERT_TRUE(resolved.first());
        EXPECT_EQ(resolved.second(), markerName);
        ++markerIndex;
    }
}


TEST(GpuCrashMarkerTracker, RetainedResolvedViewSurvivesConcurrentHistoryGrowthAndTrackerDestruction){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    usize retainedHash = 0u;
    {
        Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
        retainedHash = commandListTracker.pushEvent("Retained marker view");
    }
    const Core::ResolvedMarker retained = crashTracker.resolveMarker(retainedHash);
    ASSERT_TRUE(retained.first());
    ASSERT_EQ(retained.second(), AStringView("Retained marker view"));

    constexpr AStringView firstWriterMarkerNames[] = {
        "Concurrent writer A marker 00", "Concurrent writer A marker 01",
        "Concurrent writer A marker 02", "Concurrent writer A marker 03",
        "Concurrent writer A marker 04", "Concurrent writer A marker 05",
        "Concurrent writer A marker 06", "Concurrent writer A marker 07",
        "Concurrent writer A marker 08", "Concurrent writer A marker 09",
        "Concurrent writer A marker 10", "Concurrent writer A marker 11",
        "Concurrent writer A marker 12", "Concurrent writer A marker 13",
        "Concurrent writer A marker 14", "Concurrent writer A marker 15",
    };
    constexpr AStringView secondWriterMarkerNames[] = {
        "Concurrent writer B marker 00", "Concurrent writer B marker 01",
        "Concurrent writer B marker 02", "Concurrent writer B marker 03",
        "Concurrent writer B marker 04", "Concurrent writer B marker 05",
        "Concurrent writer B marker 06", "Concurrent writer B marker 07",
        "Concurrent writer B marker 08", "Concurrent writer B marker 09",
        "Concurrent writer B marker 10", "Concurrent writer B marker 11",
        "Concurrent writer B marker 12", "Concurrent writer B marker 13",
        "Concurrent writer B marker 14", "Concurrent writer B marker 15",
    };
    Latch growthStart(3u);
    Latch growthMidpoint(3u);
    Atomic<u32> activeWriters{ 2u };
    Atomic<bool> invalidObservation{ false };
    const auto insertMarkers = [&]<usize MarkerCount>(const AStringView (&markerNames)[MarkerCount]){
        {
            Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
            growthStart.arrive_and_wait();
            for(usize markerIndex = 0u; markerIndex < MarkerCount; ++markerIndex){
                if(markerIndex == MarkerCount / 2u)
                    growthMidpoint.arrive_and_wait();

                commandListTracker.resetEventStack();
                const usize insertedHash = commandListTracker.pushEvent(markerNames[markerIndex].data());
                const Core::ResolvedMarker inserted = crashTracker.resolveMarker(insertedHash);
                if(!inserted.first() || inserted.second() != markerNames[markerIndex])
                    invalidObservation.store(true, MemoryOrder::release);
            }
        }
        const u32 previousActiveWriters = activeWriters.fetch_sub(1u, MemoryOrder::acq_rel);
        if(previousActiveWriters == 0u || previousActiveWriters > 2u)
            invalidObservation.store(true, MemoryOrder::release);
    };
    Thread firstWriter([&](){ insertMarkers(firstWriterMarkerNames); });
    Thread secondWriter([&](){ insertMarkers(secondWriterMarkerNames); });

    const auto retainedViewIsValid = [&](){
        const Core::ResolvedMarker resolved = crashTracker.resolveMarker(retainedHash);
        return
            retained.second() == AStringView("Retained marker view")
            && resolved.first()
            && resolved.second() == AStringView("Retained marker view")
        ;
    };
    growthStart.arrive_and_wait();
    if(!retainedViewIsValid())
        invalidObservation.store(true, MemoryOrder::release);
    growthMidpoint.arrive_and_wait();
    do{
        if(!retainedViewIsValid())
            invalidObservation.store(true, MemoryOrder::release);
        YieldThread();
    }while(activeWriters.load(MemoryOrder::acquire) != 0u);

    firstWriter.join();
    secondWriter.join();

    EXPECT_FALSE(invalidObservation.load(MemoryOrder::acquire));
    EXPECT_EQ(retained.second(), AStringView("Retained marker view"));
    const Core::ResolvedMarker resolvedAgain = crashTracker.resolveMarker(retainedHash);
    ASSERT_TRUE(resolvedAgain.first());
    EXPECT_EQ(resolvedAgain.second(), retained.second());
}


TEST(GpuCrashMarkerTracker, ExactDuplicatePathsReuseIdsAndDistinctPathsResolveIndependently){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);

    const usize firstRootHash = commandListTracker.pushEvent("Duplicate root");
    const usize firstDuplicateHash = commandListTracker.pushEvent("Duplicate leaf");
    commandListTracker.resetEventStack();
    const usize secondRootHash = commandListTracker.pushEvent("Duplicate root");
    const usize secondDuplicateHash = commandListTracker.pushEvent("Duplicate leaf");
    commandListTracker.resetEventStack();
    const usize distinctHash = commandListTracker.pushEvent("Distinct path");

    EXPECT_EQ(firstRootHash, secondRootHash);
    EXPECT_EQ(firstDuplicateHash, secondDuplicateHash);
    EXPECT_NE(firstDuplicateHash, distinctHash);
    const Core::ResolvedMarker duplicate = crashTracker.resolveMarker(firstDuplicateHash);
    const Core::ResolvedMarker distinct = crashTracker.resolveMarker(distinctHash);
    ASSERT_TRUE(duplicate.first());
    ASSERT_TRUE(distinct.first());
    EXPECT_EQ(duplicate.second(), AStringView("Duplicate root/Duplicate leaf"));
    EXPECT_EQ(distinct.second(), AStringView("Distinct path"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

