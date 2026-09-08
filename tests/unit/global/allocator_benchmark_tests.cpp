// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/general.h>
#include <core/alloc/persistent.h>

#include <global/algorithm.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_allocator_benchmark_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_BatchCount = 2048u;
constexpr usize s_BatchSize = 64u;
constexpr usize s_AllocationBytes = 128u;
constexpr usize s_EpochCount = 4u;
constexpr usize s_WarmupSamplesPerEpoch = 3u;
constexpr usize s_MeasuredSamplesPerEpoch = 8u;
constexpr usize s_MeasuredSampleCount = s_EpochCount * s_MeasuredSamplesPerEpoch;


struct ChurnMeasurement{
    u64 elapsedNanoseconds = 0u;
    u64 checksum = 0u;
    bool allocationFailed = false;
    bool contentsPreserved = true;
    bool alignmentPreserved = true;
};

struct TimingSummary{
    u64 medianNanoseconds = 0u;
    u64 p95Nanoseconds = 0u;
    u64 checksum = 0u;
    bool allocationFailed = false;
    bool contentsPreserved = true;
    bool alignmentPreserved = true;
};

struct ArenaComparison{
    TimingSummary global;
    TimingSummary persistent;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arena>
[[nodiscard]] ChurnMeasurement RunAllocationChurn(Arena& arena, const usize alignment){
    Array<u8*, s_BatchSize> allocations{};
    ChurnMeasurement result;

    const Timer begin = TimerNow();
    for(usize batch = 0u; batch < s_BatchCount; ++batch){
        usize preparedCount = 0u;
        for(usize slot = 0u; slot < s_BatchSize; ++slot){
            auto* const allocation = static_cast<u8*>(arena.allocate(alignment, s_AllocationBytes));
            if(!allocation){
                result.allocationFailed = true;
                break;
            }

            allocations[slot] = allocation;
            ++preparedCount;
            result.alignmentPreserved &= reinterpret_cast<usize>(allocation) % alignment == 0u;

            volatile u8* const bytes = allocation;
            bytes[0u] = static_cast<u8>(slot);
            bytes[s_AllocationBytes - 1u] = static_cast<u8>(batch);
        }

        while(preparedCount != 0u){
            const usize slot = --preparedCount;
            u8* const allocation = allocations[slot];
            const volatile u8* const bytes = allocation;
            const u8 firstByte = bytes[0u];
            const u8 lastByte = bytes[s_AllocationBytes - 1u];
            result.contentsPreserved &= firstByte == static_cast<u8>(slot);
            result.contentsPreserved &= lastByte == static_cast<u8>(batch);
            result.checksum += static_cast<u64>(firstByte) + lastByte;
            arena.deallocate(allocation, alignment, s_AllocationBytes);
        }

        if(result.allocationFailed)
            break;
    }
    result.elapsedNanoseconds = DurationInNS<u64>(TimerNow(), begin);
    return result;
}

inline void AccumulateMeasurement(TimingSummary& summary, const ChurnMeasurement& measurement){
    summary.checksum += measurement.checksum;
    summary.allocationFailed |= measurement.allocationFailed;
    summary.contentsPreserved &= measurement.contentsPreserved;
    summary.alignmentPreserved &= measurement.alignmentPreserved;
}

[[nodiscard]] TimingSummary SummarizeMeasurements(
    const Array<u64, s_MeasuredSampleCount>& samples,
    TimingSummary summary
){
    Array<u64, s_MeasuredSampleCount> orderedSamples = samples;
    Sort(orderedSamples.begin(), orderedSamples.end());
    summary.medianNanoseconds =
        (orderedSamples[s_MeasuredSampleCount / 2u - 1u] + orderedSamples[s_MeasuredSampleCount / 2u]) / 2u
    ;
    constexpr usize s_P95Index = (s_MeasuredSampleCount * 95u + 99u) / 100u - 1u;
    summary.p95Nanoseconds = orderedSamples[s_P95Index];
    return summary;
}

template<typename Arena>
inline void RunWarmup(Arena& arena, const usize alignment, TimingSummary& summary){
    const ChurnMeasurement measurement = RunAllocationChurn(arena, alignment);
    AccumulateMeasurement(summary, measurement);
}

template<typename Arena>
inline void RunMeasuredSample(
    Arena& arena,
    const usize alignment,
    TimingSummary& summary,
    Array<u64, s_MeasuredSampleCount>& samples,
    const usize sampleIndex
){
    const ChurnMeasurement measurement = RunAllocationChurn(arena, alignment);
    AccumulateMeasurement(summary, measurement);
    samples[sampleIndex] = measurement.elapsedNanoseconds;
}

[[nodiscard]] ArenaComparison CompareArenas(const usize alignment){
    NWB::Core::Alloc::GlobalArena globalArena(Name("tests/allocator_benchmark/global"));
    const usize persistentPoolBytes = s_BatchSize * NWB::Core::Alloc::PersistentArena::StructureAlignedSize(
        s_AllocationBytes,
        alignment
    );
    NWB::Core::Alloc::PersistentArena persistentArena(
        Name("tests/allocator_benchmark/persistent"),
        persistentPoolBytes
    );
    ArenaComparison comparison;
    Array<u64, s_MeasuredSampleCount> globalSamples{};
    Array<u64, s_MeasuredSampleCount> persistentSamples{};
    usize sampleIndex = 0u;

    for(usize epoch = 0u; epoch < s_EpochCount; ++epoch){
        const bool persistentFirst = (epoch & 1u) != 0u;
        for(usize warmup = 0u; warmup < s_WarmupSamplesPerEpoch; ++warmup){
            if(persistentFirst){
                RunWarmup(persistentArena, alignment, comparison.persistent);
                RunWarmup(globalArena, alignment, comparison.global);
            }
            else{
                RunWarmup(globalArena, alignment, comparison.global);
                RunWarmup(persistentArena, alignment, comparison.persistent);
            }
        }

        for(usize sample = 0u; sample < s_MeasuredSamplesPerEpoch; ++sample){
            if(persistentFirst){
                RunMeasuredSample(persistentArena, alignment, comparison.persistent, persistentSamples, sampleIndex);
                RunMeasuredSample(globalArena, alignment, comparison.global, globalSamples, sampleIndex);
            }
            else{
                RunMeasuredSample(globalArena, alignment, comparison.global, globalSamples, sampleIndex);
                RunMeasuredSample(persistentArena, alignment, comparison.persistent, persistentSamples, sampleIndex);
            }
            ++sampleIndex;
        }
    }

    EXPECT_EQ(sampleIndex, s_MeasuredSampleCount);
    EXPECT_EQ(globalArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(persistentArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(persistentArena.memoryStats().reservedBytes, persistentPoolBytes);
    comparison.global = SummarizeMeasurements(globalSamples, comparison.global);
    comparison.persistent = SummarizeMeasurements(persistentSamples, comparison.persistent);
    return comparison;
}

void RecordUnsignedProperty(const char* const key, const u64 value){
    char text[32u] = {};
    testing::Test::RecordProperty(key, FormatDecimal(value, text).data());
}

void VerifyAndRecordComparison(const usize alignment){
    const ArenaComparison comparison = CompareArenas(alignment);
    EXPECT_FALSE(comparison.global.allocationFailed);
    EXPECT_TRUE(comparison.global.contentsPreserved);
    EXPECT_TRUE(comparison.global.alignmentPreserved);
    EXPECT_FALSE(comparison.persistent.allocationFailed);
    EXPECT_TRUE(comparison.persistent.contentsPreserved);
    EXPECT_TRUE(comparison.persistent.alignmentPreserved);
    EXPECT_GT(comparison.global.checksum, 0u);
    EXPECT_EQ(comparison.global.checksum, comparison.persistent.checksum);

    RecordUnsignedProperty("alignment", alignment);
    RecordUnsignedProperty("global_median_ns", comparison.global.medianNanoseconds);
    RecordUnsignedProperty("persistent_median_ns", comparison.persistent.medianNanoseconds);
    RecordUnsignedProperty("global_p95_ns", comparison.global.p95Nanoseconds);
    RecordUnsignedProperty("persistent_p95_ns", comparison.persistent.p95Nanoseconds);
    RecordUnsignedProperty("operations_per_sample", s_BatchCount * s_BatchSize * 2u);
    RecordUnsignedProperty("measured_sample_count", s_MeasuredSampleCount);
    RecordUnsignedProperty("persistent_percent_of_global", comparison.global.medianNanoseconds != 0u
        ? comparison.persistent.medianNanoseconds * 100u / comparison.global.medianNanoseconds
        : 0u
    );

    EXPECT_LT(comparison.persistent.medianNanoseconds, comparison.global.medianNanoseconds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AllocatorPerformanceBenchmark, DISABLED_PersistentArenaSteadyStateUnalignedChurn){
    __hidden_allocator_benchmark_tests::VerifyAndRecordComparison(1u);
}

TEST(AllocatorPerformanceBenchmark, DISABLED_PersistentArenaSteadyStateAlignedChurn){
    __hidden_allocator_benchmark_tests::VerifyAndRecordComparison(256u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

