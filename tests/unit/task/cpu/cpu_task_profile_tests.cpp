// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <core/alloc/scratch.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_profile_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_WarmupCount = 3u;
inline constexpr u32 s_SampleCount = 8u;
inline constexpr AStringView s_SampleKeys[s_SampleCount] = {
    "sample_0_ns", "sample_1_ns", "sample_2_ns", "sample_3_ns",
    "sample_4_ns", "sample_5_ns", "sample_6_ns", "sample_7_ns"
};


[[nodiscard]] CpuTaskSchedulerConfig WorkerConfig(const u32 workerCount){
    CpuTaskSchedulerConfig config;
    config.workerCount = workerCount;
    config.heterogeneous = false;
    return config;
}

[[nodiscard]] u64 Work(u64 value, const u32 rounds)noexcept{
    for(u32 round = 0u; round < rounds; ++round){
        value ^= value >> 17u;
        value *= 0x9e3779b185ebca87ull;
        value ^= value >> 29u;
        value += 0xd1b54a32d192ed03ull;
    }
    return value;
}

void RecordUnsigned(const char* key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key, text);
}

void RecordSample(const u32 sample, const u64 nanoseconds, const u64 checksum){
    if(sample < s_WarmupCount)
        return;
    RecordUnsigned(s_SampleKeys[sample - s_WarmupCount].data(), nanoseconds);
    RecordUnsigned("checksum", checksum);
}

void RecordWorkers(const CpuTaskScheduler& scheduler){
    const CpuTaskSchedulerStatistics statistics = scheduler.statistics();
    RecordUnsigned("worker_count", scheduler.workerThreadCount());
    RecordUnsigned("placement_failures", statistics.placementFailures);
    RecordUnsigned("performance_workers", statistics.performanceWorkers);
    RecordUnsigned("efficiency_workers", statistics.efficiencyWorkers);
    RecordUnsigned("unclassified_workers", statistics.unclassifiedWorkers);
}

void ProfileRanges(
    Alloc::ScratchArena& scratch,
    const u32 workers,
    const usize count,
    const usize grain,
    const u32 repetitions,
    const u32 rounds){
    Vector<u64, Alloc::ScratchArena> output(count, 0u, scratch);
    Vector<u64, Alloc::ScratchArena> expected(count, 0u, scratch);
    for(usize index = 0u; index < count; ++index){
        for(u32 repetition = 0u; repetition < repetitions; ++repetition)
            expected[index] += Work(static_cast<u64>(index) + repetition + 1u, rounds);
    }
    CpuTaskScheduler scheduler(WorkerConfig(workers));
    RecordUnsigned("elements", count);
    RecordUnsigned("repetitions", repetitions);
    RecordUnsigned("rounds_per_element", rounds);
    for(u32 sample = 0u; sample < s_WarmupCount + s_SampleCount; ++sample){
        for(u64& value : output)
            value = 0u;
        const Timer begin = TimerNow();
        for(u32 repetition = 0u; repetition < repetitions; ++repetition){
            scheduler.parallelFor(0u, count, grain, [&](const usize index){
                output[index] += Work(static_cast<u64>(index) + repetition + 1u, rounds);
            });
        }
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        u64 checksum = 0u;
        for(usize index = 0u; index < count; ++index){
            ASSERT_EQ(output[index], expected[index]);
            checksum += output[index];
        }
        ASSERT_EQ(scheduler.statistics().outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, checksum);
    }
    RecordWorkers(scheduler);
}

void ProfileNestedRanges(Alloc::ScratchArena& scratch, const u32 workers){
    constexpr usize s_OuterCount = 8u;
    constexpr usize s_InnerCount = 128u;
    constexpr u32 s_Repetitions = 4u;
    Vector<u64, Alloc::ScratchArena> output(s_OuterCount * s_InnerCount, 0u, scratch);
    Vector<u64, Alloc::ScratchArena> expected(output.size(), 0u, scratch);
    for(usize index = 0u; index < expected.size(); ++index){
        for(u32 repetition = 0u; repetition < s_Repetitions; ++repetition)
            expected[index] += Work(static_cast<u64>(index) + repetition + 1u, 8u);
    }
    CpuTaskScheduler scheduler(WorkerConfig(workers));
    RecordUnsigned("elements", output.size());
    RecordUnsigned("repetitions", s_Repetitions);
    for(u32 sample = 0u; sample < s_WarmupCount + s_SampleCount; ++sample){
        for(u64& value : output)
            value = 0u;
        const Timer begin = TimerNow();
        for(u32 repetition = 0u; repetition < s_Repetitions; ++repetition){
            scheduler.parallelFor(0u, s_OuterCount, [&](const usize outer){
                scheduler.parallelFor(0u, s_InnerCount, [&](const usize inner){
                    const usize index = outer * s_InnerCount + inner;
                    output[index] += Work(static_cast<u64>(index) + repetition + 1u, 8u);
                });
            });
        }
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        u64 checksum = 0u;
        for(usize index = 0u; index < output.size(); ++index){
            ASSERT_EQ(output[index], expected[index]);
            checksum += output[index];
        }
        ASSERT_EQ(scheduler.statistics().outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, checksum);
    }
    RecordWorkers(scheduler);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opt-in measurements: run with --gtest_also_run_disabled_tests --gtest_filter=CpuTaskProfile.* and XML output.
// Every raw sample excludes scheduler initialization, reference work, output clearing, validation, and reporting.
// Preserve the baseline binary and run baseline/candidate in alternating order; timing is never a pass threshold.

TEST(CpuTaskProfile, DISABLED_SingleChunkRanges){
    NWB::Core::Alloc::ScratchArena scratch("tests/task/cpu/profile_single_chunk");
    __hidden_cpu_task_profile_tests::ProfileRanges(scratch, 1u, 16u, 16u, 128u, 4u);
}

TEST(CpuTaskProfile, DISABLED_TinyBatchedRanges){
    NWB::Core::Alloc::ScratchArena scratch("tests/task/cpu/profile_tiny_batch");
    __hidden_cpu_task_profile_tests::ProfileRanges(scratch, 4u, 1024u, 1u, 32u, 4u);
}

TEST(CpuTaskProfile, DISABLED_CoarseRangeControl){
    NWB::Core::Alloc::ScratchArena scratch("tests/task/cpu/profile_coarse_range");
    __hidden_cpu_task_profile_tests::ProfileRanges(scratch, 4u, 16384u, 64u, 2u, 64u);
}

TEST(CpuTaskProfile, DISABLED_NestedRangesOneWorker){
    NWB::Core::Alloc::ScratchArena scratch("tests/task/cpu/profile_nested_one");
    __hidden_cpu_task_profile_tests::ProfileNestedRanges(scratch, 1u);
}

TEST(CpuTaskProfile, DISABLED_NestedRangesFourWorkers){
    NWB::Core::Alloc::ScratchArena scratch("tests/task/cpu/profile_nested_four");
    __hidden_cpu_task_profile_tests::ProfileNestedRanges(scratch, 4u);
}

TEST(CpuTaskProfile, DISABLED_UnrelatedScopeJoin){
    using namespace __hidden_cpu_task_profile_tests;
    constexpr usize s_RootCount = 128u;
    constexpr usize s_ChainCount = 64u;
    constexpr usize s_ScopeCount = 32u;
    Alloc::ScratchArena scratch("tests/task/cpu/profile_scope_join");
    Vector<CpuTaskHandle, Alloc::ScratchArena> roots(s_RootCount, CpuTaskHandle{}, scratch);
    RecordUnsigned("worker_count", 0u);
    RecordUnsigned("unrelated_roots", s_RootCount);
    RecordUnsigned("unrelated_chain", s_ChainCount);
    RecordUnsigned("scope_tasks", s_ScopeCount);
    for(u32 sample = 0u; sample < s_WarmupCount + s_SampleCount; ++sample){
        u32 unrelatedInvocations = 0u;
        u32 joinedInvocations = 0u;
        CpuTaskScheduler scheduler(0u);
        CpuTaskScope joined(scheduler);
        for(usize index = 0u; index < s_RootCount; ++index){
            roots[index] = scheduler.submit([&](){ ++unrelatedInvocations; });
            ASSERT_TRUE(roots[index].valid());
        }
        CpuTaskHandle predecessor = scheduler.submit([&](){ ++unrelatedInvocations; }, {}, roots.data(), roots.size());
        ASSERT_TRUE(predecessor.valid());
        for(usize index = 1u; index < s_ChainCount; ++index){
            predecessor = scheduler.submit([&](){ ++unrelatedInvocations; }, predecessor);
            ASSERT_TRUE(predecessor.valid());
        }
        for(usize index = 0u; index < s_ScopeCount; ++index)
            ASSERT_TRUE(joined.submit([&](){ ++joinedInvocations; }).valid());

        const Timer begin = TimerNow();
        joined.wait();
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_EQ(joinedInvocations, s_ScopeCount);
        ASSERT_EQ(unrelatedInvocations, 0u);
        scheduler.wait();
        ASSERT_EQ(unrelatedInvocations, s_RootCount + s_ChainCount);
        ASSERT_EQ(scheduler.statistics().outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, joinedInvocations + unrelatedInvocations);
    }
}

TEST(CpuTaskProfile, DISABLED_CanceledHistoryLookups){
    using namespace __hidden_cpu_task_profile_tests;
    constexpr usize s_HistoryCount = 4096u;
    constexpr usize s_LookupCount = 512u;
    Alloc::ScratchArena scratch("tests/task/cpu/profile_canceled_history");
    Vector<CpuTaskHandle, Alloc::ScratchArena> history(s_HistoryCount, CpuTaskHandle{}, scratch);
    RecordUnsigned("worker_count", 0u);
    RecordUnsigned("canceled_history", s_HistoryCount);
    RecordUnsigned("lookups", s_LookupCount);
    for(u32 sample = 0u; sample < s_WarmupCount + s_SampleCount; ++sample){
        u32 invocations = 0u;
        CpuTaskScheduler scheduler(0u);
        CpuTaskScope canceled(scheduler);
        for(usize index = 0u; index < s_HistoryCount; ++index){
            history[index] = canceled.submit([&](){ ++invocations; });
            ASSERT_TRUE(history[index].valid());
        }
        canceled.cancel();
        canceled.wait();
        ASSERT_EQ(invocations, 0u);
        ASSERT_EQ(scheduler.statistics().canceledTasks, s_HistoryCount);

        bool accepted = true;
        const Timer begin = TimerNow();
        for(usize index = 0u; index < s_LookupCount; ++index){
            const CpuTaskHandle dependency = history[(index * 4051u) % s_HistoryCount];
            const CpuTaskHandle task = scheduler.submit([&](){ ++invocations; }, dependency);
            accepted = task.valid() && accepted;
        }
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_TRUE(accepted);
        scheduler.wait();
        const CpuTaskSchedulerStatistics statistics = scheduler.statistics();
        ASSERT_EQ(invocations, 0u);
        ASSERT_EQ(statistics.canceledTasks, s_HistoryCount + s_LookupCount);
        ASSERT_EQ(statistics.outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, statistics.canceledTasks);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

