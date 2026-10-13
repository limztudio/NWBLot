// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/expected.h>
#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/timer.h>
#include <global/text_utils.h>

#include <cerrno>
#include <iostream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimdOptimizationBenchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DataCount = 64u;
inline constexpr u64 s_InitialSeed = 0x8923409102431291ull;

struct Dataset{
    SIMDVectorDouble left[s_DataCount];
    SIMDVectorDouble right[s_DataCount];
    f64 angles[s_DataCount];
};

inline u64 NextSeed(u64& seed)noexcept{
    seed ^= seed << 13u;
    seed ^= seed >> 7u;
    seed ^= seed << 17u;
    return seed;
}

NWB_NOINLINE inline void InitializeDataset(Dataset& dataset, u64 seed)noexcept{
    for(usize index = 0u; index < s_DataCount; ++index){
        f64 left[4], right[4];
        for(usize lane = 0u; lane < 4u; ++lane){
            left[lane] = static_cast<f64>(NextSeed(seed) % 8192u) * 0x1p-10 - 4.0;
            right[lane] = static_cast<f64>(NextSeed(seed) % 64u + 1u) * 0x1p-10;
        }
        dataset.left[index] = VectorDoubleSet(left[0], left[1], left[2], left[3]);
        dataset.right[index] = VectorDoubleSet(right[0], right[1], right[2], right[3]);
        dataset.angles[index] = static_cast<f64>(NextSeed(seed) % 2048u + 1u) * 0x1p-10;
    }
}

NWB_INLINE bool EqualBits(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    for(usize lane = 0u; lane < 4u; ++lane){
        if(VectorDoubleGetIntByIndex(lhs, lane) != VectorDoubleGetIntByIndex(rhs, lane))
            return false;
    }
    return true;
}

struct Observation{
    SIMDVectorDouble value;
    i32 flags;
    i32 error;
};

template<typename Evaluate>
[[nodiscard]] Expected<Observation> Observe(Evaluate&& evaluate)noexcept{
#pragma STDC FENV_ACCESS ON
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return MakeUnexpected(Failure{});
    errno = E2BIG;
    Observation observation;
    observation.value = evaluate();
    observation.flags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    observation.error = errno;
    return observation;
}

inline bool EqualObservation(const Observation& lhs, const Observation& rhs)noexcept{
    return EqualBits(lhs.value, rhs.value) && lhs.flags == rhs.flags && lhs.error == rhs.error;
}

struct NoAdditionalReductionCheck{
    bool operator()(const SIMDVectorDouble, const SIMDVectorDouble, const Observation&, const Observation&, const usize, u32&)const noexcept{ return true; }
};

template<typename Baseline, typename Candidate, typename Additional = NoAdditionalReductionCheck>
u32 VerifyReductionCases(const StringView operation, u64& seed, Baseline&& baseline, Candidate&& candidate, Additional&& additional = {})noexcept{
#pragma STDC FENV_ACCESS ON
    constexpr u64 exceptional[]{ 0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull };
    u32 failures = 0u;
    for(usize index = 0u; index < 16384u; ++index){
        u64 bits[8];
        for(auto& value : bits){
            const u64 random = NextSeed(seed);
            value = index < 8192u ? exceptional[random % LengthOf(exceptional)] : random;
        }
        const SIMDVectorDouble lhs = VectorDoubleSetInt(bits[0], bits[1], bits[2], bits[3]), rhs = VectorDoubleSetInt(bits[4], bits[5], bits[6], bits[7]);
        const auto expected = Observe([&]()noexcept{ return baseline(lhs, rhs); });
        if(!expected)
            return failures + 1u;
        const auto actual = Observe([&]()noexcept{ return candidate(lhs, rhs); });
        if(!actual)
            return failures + 1u;
        if(!additional(lhs, rhs, *expected, *actual, index, failures))
            return failures + 1u;
        if(!EqualObservation(*expected, *actual)){
            if(++failures <= 3u)
                NWB_COUT << "reduction mismatch operation=" << operation << " case=" << index << " flags=" << expected->flags << '/' << actual->flags << " errno=" << expected->error << '/' << actual->error << '\n';
        }
    }
    return failures;
}

struct VerificationSweep{
    u32 failures;
    bool environmentAvailable;
};

template<typename Verify>
VerificationSweep VerifyRoundingModes(Verify&& verify)noexcept{
#pragma STDC FENV_ACCESS ON
    const auto environment = HoldFloatingPointExceptions();
    if(!environment)
        return { 1u, false };
    constexpr i32 modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    u64 seed = s_InitialSeed;
    u32 failures = 0u;
    for(const i32 mode : modes){
        if(!SetFloatingPointRoundingMode(mode)){
            ++failures;
            break;
        }
        failures += verify(seed);
    }
    if(!SetFloatingPointEnvironment(*environment))
        ++failures;
    return { failures, true };
}

template<typename Workload>
void Measure(const StringView operation, const bool candidate, const bool latency, const usize operations, Workload&& workload){
    const Timer start = TimerNow();
    const SIMDVectorDouble result = workload();
    const f64 nanoseconds = DurationInNS<f64>(TimerNow(), start);
    u64 checksum = 0u;
    for(usize lane = 0u; lane < 4u; ++lane)
        checksum ^= VectorDoubleGetIntByIndex(result, lane) + static_cast<u64>(lane);
    NWB_COUT
        << "{\"operation\":\"" << operation << "\",\"variant\":\"" << (candidate ? "candidate" : "baseline")
        << "\",\"workload\":\"" << (latency ? "latency" : "throughput") << "\",\"operations\":" << operations
        << ",\"nanoseconds\":" << nanoseconds << ",\"ns_per_operation\":" << nanoseconds / static_cast<f64>(operations)
        << ",\"checksum\":\"" << checksum << "\"}\n"
    ;
}

struct MeasureOptions{
    bool candidate;
    bool latency;
    usize iterations;
};

[[nodiscard]] inline Expected<MeasureOptions> ParseMeasureOptions(const char* const* argv, const usize maximumIterations, const bool allowLatency){
    const bool candidate = StringView(argv[3]) == "candidate";
    if(!candidate && StringView(argv[3]) != "baseline")
        return MakeUnexpected(Failure{});
    const bool latency = StringView(argv[5]) == "latency";
    if((latency && !allowLatency) || (!latency && StringView(argv[5]) != "throughput"))
        return MakeUnexpected(Failure{});
    const auto parsedIterations = ParseU64FromChars(AStringView(argv[4]));
    if(!parsedIterations || *parsedIterations == 0u || *parsedIterations > maximumIterations)
        return MakeUnexpected(Failure{});
    return MeasureOptions{ candidate, latency, static_cast<usize>(*parsedIterations) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

