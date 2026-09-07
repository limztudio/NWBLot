// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/job.h>

#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_job_system_fanin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RootReleaseGuard{
    AtomicFlag& releaseRoot;


    ~RootReleaseGuard()noexcept{
        releaseRoot.test_and_set(MemoryOrder::release);
        releaseRoot.notify_all();
    }
};

struct DependencySubmissionTimings{
    u64 fanOutNanoseconds;
    u64 fanInNanoseconds;
};

[[nodiscard]] static DependencySubmissionTimings VerifyFanIn(
    NWB::Core::Alloc::ScratchArena& scratchArena,
    const usize uniqueDependencyCount,
    const usize repetitionCount){
    using JobHandle = NWB::Core::Alloc::JobSystem::JobHandle;

    AtomicFlag rootEntered;
    AtomicFlag releaseRoot;
    Atomic<u32> dependencyCompletionCount{ 0u };
    Atomic<u32> completionCountObservedByJoin{ 0u };
    Atomic<u32> joinInvocationCount{ 0u };
    constexpr usize s_ArenaBytes = 16u * 1024u * 1024u;
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any, s_ArenaBytes);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool, s_ArenaBytes);
    RootReleaseGuard releaseGuard{ releaseRoot };

    const JobHandle completed = jobSystem.submit([]()noexcept{});
    EXPECT_TRUE(completed.valid());
    jobSystem.wait(completed);

    const JobHandle root = jobSystem.submit([&](){
        rootEntered.test_and_set(MemoryOrder::release);
        rootEntered.notify_all();
        while(!releaseRoot.test(MemoryOrder::acquire))
            releaseRoot.wait(false, MemoryOrder::acquire);
    });
    EXPECT_TRUE(root.valid());
    while(!rootEntered.test(MemoryOrder::acquire))
        rootEntered.wait(false, MemoryOrder::acquire);

    Vector<JobHandle, NWB::Core::Alloc::ScratchArena> jobs(scratchArena);
    Vector<JobHandle, NWB::Core::Alloc::ScratchArena> dependencies(scratchArena);
    jobs.reserve(uniqueDependencyCount);
    dependencies.reserve(uniqueDependencyCount * repetitionCount + 3u);
    const Timer fanOutBegin = TimerNow();
    for(usize i = 0u; i < uniqueDependencyCount; ++i){
        const JobHandle dependency = jobSystem.submit([&](){
            dependencyCompletionCount.fetch_add(1u, MemoryOrder::release);
        }, root);
        EXPECT_TRUE(dependency.valid());
        jobs.push_back(dependency);
    }
    const u64 fanOutNanoseconds = DurationInNS<u64>(TimerNow(), fanOutBegin);

    // An odd stride permutes the power-of-two job counts, with duplicate occurrences separated by a full pass.
    for(usize repeat = 0u; repeat < repetitionCount; ++repeat){
        for(usize i = 0u; i < uniqueDependencyCount; ++i)
            dependencies.push_back(jobs[(i * 4051u + repeat) % uniqueDependencyCount]);
    }
    JobHandle foreign = root;
    ++foreign.domainIdentity;
    dependencies.push_back(foreign);
    dependencies.push_back(completed);
    dependencies.push_back(JobHandle{});

    const Timer submitBegin = TimerNow();
    const JobHandle joined = jobSystem.submit([&](){
        completionCountObservedByJoin.store(dependencyCompletionCount.load(MemoryOrder::acquire), MemoryOrder::release);
        joinInvocationCount.fetch_add(1u, MemoryOrder::release);
    }, dependencies.data(), dependencies.size());
    const u64 submitNanoseconds = DurationInNS<u64>(TimerNow(), submitBegin);
    EXPECT_TRUE(joined.valid());
    EXPECT_EQ(dependencyCompletionCount.load(MemoryOrder::acquire), 0u);
    EXPECT_EQ(joinInvocationCount.load(MemoryOrder::acquire), 0u);

    releaseRoot.test_and_set(MemoryOrder::release);
    releaseRoot.notify_all();
    jobSystem.wait(joined);
    jobSystem.finish();
    EXPECT_EQ(dependencyCompletionCount.load(MemoryOrder::acquire), uniqueDependencyCount);
    EXPECT_EQ(completionCountObservedByJoin.load(MemoryOrder::acquire), uniqueDependencyCount);
    EXPECT_EQ(joinInvocationCount.load(MemoryOrder::acquire), 1u);
    return { fanOutNanoseconds, submitNanoseconds };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(JobSystemTests, LargeDistinctFanInWaitsForEveryDependency){
    NWB::Core::Alloc::ScratchArena scratchArena("JobSystemTests.LargeDistinctFanIn");
    const auto timings = __hidden_job_system_fanin_tests::VerifyFanIn(scratchArena, 4096u, 1u);
    RecordProperty("fan_out_submission_ns", StringFormat(scratchArena, "{}", timings.fanOutNanoseconds).c_str());
    RecordProperty("fan_in_submission_ns", StringFormat(scratchArena, "{}", timings.fanInNanoseconds).c_str());
}

TEST(JobSystemTests, LargeRepeatedFanInPublishesExactlyOneJoin){
    NWB::Core::Alloc::ScratchArena scratchArena("JobSystemTests.LargeRepeatedFanIn");
    const auto timings = __hidden_job_system_fanin_tests::VerifyFanIn(scratchArena, 1024u, 4u);
    RecordProperty("fan_out_submission_ns", StringFormat(scratchArena, "{}", timings.fanOutNanoseconds).c_str());
    RecordProperty("fan_in_submission_ns", StringFormat(scratchArena, "{}", timings.fanInNanoseconds).c_str());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

