// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/arena_memory.h>
#include <global/containers.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scratch_benchmark_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Core::Alloc::ScratchArena;

constexpr usize s_WarmupSampleCount = 8u;
constexpr usize s_MeasuredSampleCount = 32u;
constexpr usize s_ColdIterationsPerSample = 32768u;
constexpr usize s_WarmIterationsPerSample = 262144u;
constexpr usize s_MixedBatchesPerSample = 32768u;

struct AllocationRequest{
    usize alignment = 1u;
    usize bytes = 1u;
};

struct Sample{
    u64 elapsedNanoseconds = 0u;
    u64 checksum = 0u;
    bool valid = true;
};

struct Summary{
    u64 medianNanoseconds = 0u;
    u64 p95Nanoseconds = 0u;
    u64 checksum = 0u;
    u64 heapAllocationCount = 0u;
    u64 heapDeallocationCount = 0u;
    bool valid = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool TouchAllocation(
    u8* const allocation,
    const usize alignment,
    const usize bytes,
    const usize serial,
    volatile u64& checksum
){
    if(!allocation || bytes == 0u || reinterpret_cast<usize>(allocation) % alignment != 0u)
        return false;

    const u8 first = static_cast<u8>((serial * 29u + bytes) & 0xffu);
    const u8 last = static_cast<u8>((serial * 47u + alignment) & 0xffu);
    volatile u8* const writable = allocation;
    writable[0u] = first;
    writable[bytes - 1u] = last;

    const volatile u8* const readable = allocation;
    const u8 observedFirst = readable[0u];
    const u8 observedLast = readable[bytes - 1u];
    checksum = checksum + static_cast<u64>(observedFirst) + static_cast<u64>(observedLast);
    return observedFirst == first && observedLast == last;
}

template<typename RunSampleT>
[[nodiscard]] static Summary Measure(RunSampleT&& runSample){
    for(usize warmup = 0u; warmup < s_WarmupSampleCount; ++warmup){
        const Sample sample = runSample();
        if(!sample.valid)
            return Summary{ .valid = false };
    }

    const ArenaMemoryStats beforeHeap = HeapBackingMemoryStats();
    Array<u64, s_MeasuredSampleCount> elapsedSamples{};
    Summary summary;
    for(usize sampleIndex = 0u; sampleIndex < s_MeasuredSampleCount; ++sampleIndex){
        const Sample sample = runSample();
        elapsedSamples[sampleIndex] = sample.elapsedNanoseconds;
        summary.checksum += sample.checksum;
        summary.valid &= sample.valid;
    }
    const ArenaMemoryStats afterHeap = HeapBackingMemoryStats();

    Sort(elapsedSamples.begin(), elapsedSamples.end());
    const usize lowerMedianIndex = s_MeasuredSampleCount / 2u - 1u;
    const usize upperMedianIndex = s_MeasuredSampleCount / 2u;
    summary.medianNanoseconds = elapsedSamples[lowerMedianIndex]
        + (elapsedSamples[upperMedianIndex] - elapsedSamples[lowerMedianIndex]) / 2u
    ;
    constexpr usize s_P95Index = (s_MeasuredSampleCount * 95u + 99u) / 100u - 1u;
    summary.p95Nanoseconds = elapsedSamples[s_P95Index];
    summary.heapAllocationCount = afterHeap.allocationCount - beforeHeap.allocationCount;
    summary.heapDeallocationCount = afterHeap.deallocationCount - beforeHeap.deallocationCount;
    return summary;
}

static void RecordUnsignedProperty(const char* const key, const u64 value){
    char text[32u] = {};
    testing::Test::RecordProperty(key, FormatDecimal(value, text).data());
}

static void VerifyAndRecord(
    const Summary& summary,
    const usize operationsPerSample,
    const usize arenaLifecyclesPerSample = 0u
){
    EXPECT_TRUE(summary.valid);
    EXPECT_GT(summary.checksum, 0u);

    RecordUnsignedProperty("median_ns", summary.medianNanoseconds);
    RecordUnsignedProperty("p95_ns", summary.p95Nanoseconds);
    RecordUnsignedProperty("operations_per_sample", operationsPerSample);
    RecordUnsignedProperty("arena_lifecycles_per_sample", arenaLifecyclesPerSample);
    RecordUnsignedProperty("warmup_sample_count", s_WarmupSampleCount);
    RecordUnsignedProperty("measured_sample_count", s_MeasuredSampleCount);
    RecordUnsignedProperty("heap_allocation_count", summary.heapAllocationCount);
    RecordUnsignedProperty("heap_deallocation_count", summary.heapDeallocationCount);
    RecordUnsignedProperty("checksum", summary.checksum);
}

[[nodiscard]] static Sample RunColdArenaSample(){
    Sample result;
    volatile u64 checksum = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < s_ColdIterationsPerSample; ++iteration){
        ScratchArena arena(Name("tests/scratch_benchmark/cold"), 256u);
        auto* const allocation = static_cast<u8*>(arena.allocate(64u, 384u));
        result.valid &= TouchAllocation(allocation, 64u, 384u, iteration, checksum);
    }
    result.elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    result.checksum = checksum;
    return result;
}

[[nodiscard]] static Sample RunWarmSingleAllocationSample(ScratchArena& arena){
    Sample result;
    volatile u64 checksum = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < s_WarmIterationsPerSample; ++iteration){
        auto* const allocation = static_cast<u8*>(arena.allocate(8u, 192u));
        result.valid &= TouchAllocation(allocation, 8u, 192u, iteration, checksum);
        arena.deallocate(allocation, 8u, 192u);
    }
    result.elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    result.checksum = checksum;
    return result;
}

template<usize RequestCount>
[[nodiscard]] static Sample RunMixedAlignmentSample(
    ScratchArena& arena,
    const AllocationRequest (&requests)[RequestCount]
){
    Sample result;
    volatile u64 checksum = 0u;
    const Timer begin = TimerNow();
    for(usize batch = 0u; batch < s_MixedBatchesPerSample; ++batch){
        Array<u8*, RequestCount> allocations{};
        usize preparedCount = 0u;
        for(; preparedCount < RequestCount; ++preparedCount){
            const AllocationRequest& request = requests[preparedCount];
            auto* const allocation = static_cast<u8*>(arena.allocate(request.alignment, request.bytes));
            if(!TouchAllocation(
                allocation,
                request.alignment,
                request.bytes,
                batch * RequestCount + preparedCount,
                checksum
            )){
                result.valid = false;
                break;
            }
            allocations[preparedCount] = allocation;
        }
        while(preparedCount != 0u){
            const usize index = --preparedCount;
            const AllocationRequest& request = requests[index];
            arena.deallocate(allocations[index], request.alignment, request.bytes);
        }
        if(!result.valid)
            break;
    }
    result.elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    result.checksum = checksum;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ScratchArenaPerformanceBenchmark, DISABLED_ColdArenaChunkCreateDestroy){
    const __hidden_scratch_benchmark_tests::Summary summary = __hidden_scratch_benchmark_tests::Measure(
        [](){ return __hidden_scratch_benchmark_tests::RunColdArenaSample(); }
    );
    __hidden_scratch_benchmark_tests::VerifyAndRecord(
        summary,
        __hidden_scratch_benchmark_tests::s_ColdIterationsPerSample,
        __hidden_scratch_benchmark_tests::s_ColdIterationsPerSample
    );
}

TEST(ScratchArenaPerformanceBenchmark, DISABLED_WarmSingleAllocationLifoReuse){
    using namespace __hidden_scratch_benchmark_tests;
    ScratchArena arena(Name("tests/scratch_benchmark/warm_single"), 256u);
    const Summary summary = Measure([&](){ return RunWarmSingleAllocationSample(arena); });
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    VerifyAndRecord(summary, s_WarmIterationsPerSample * 2u);
}

TEST(ScratchArenaPerformanceBenchmark, DISABLED_WarmedMixedRuntimeAlignmentMultiChunkReuse){
    using namespace __hidden_scratch_benchmark_tests;
    constexpr AllocationRequest s_Requests[]{
        { 1u, 192u }, { 8u, 192u }, { 64u, 192u }, { 256u, 256u },
        { 1u, 768u }, { 8u, 768u }, { 64u, 768u }, { 256u, 768u },
        { 1u, 4096u }, { 8u, 4096u }, { 64u, 4096u }, { 256u, 4096u },
    };
    ScratchArena arena(Name("tests/scratch_benchmark/mixed_alignment"), 256u);
    const Summary summary = Measure([&](){ return RunMixedAlignmentSample(arena, s_Requests); });
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
    VerifyAndRecord(summary, s_MixedBatchesPerSample * LengthOf(s_Requests) * 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

