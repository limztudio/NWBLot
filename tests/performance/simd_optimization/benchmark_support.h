// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/floating_point_environment_guard.h>

#include <global/array.h>
#include <global/diagnostics.h>
#include <global/expected.h>
#include <global/floating_point_environment.h>
#include <global/scope_exit.h>
#include <global/simdmath.h>
#include <global/text_utils.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimdOptimizationBenchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DataCount = 64u;
inline constexpr u64 s_InitialSeed = 0x8923409102431291ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Dataset{
    Array<Double4, s_DataCount> left = {};
    Array<Double4, s_DataCount> right = {};
    Array<f64, s_DataCount> angles = {};
};

struct alignas(SIMDVectorDouble) Observation{
    SIMDVectorDouble value;
    i32 flags = 0;
    i32 error = 0;
};
static_assert(alignof(Expected<Observation>) >= alignof(Observation));

struct NoAdditionalReductionCheck{
    b8 operator()(const SIMDVectorDouble, const SIMDVectorDouble, const Observation&, const Observation&, const usize, u32&)const noexcept{ return true; }
};

struct VerificationSweep{
    u32 failures = 0u;
    b8 environmentAvailable = false;
};

struct MeasureOptions{
    b8 candidate = false;
    b8 latency = false;
    usize iterations = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 NextSeed(u64& seed)noexcept;
NWB_NOINLINE void InitializeDataset(u64 seed, Dataset& dataset)noexcept;
[[nodiscard]] b8 EqualBits(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept;

[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL BaselineDot2(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL BaselineDot3(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL BaselineDot4(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL BaselineSum(SIMDVectorDouble value)noexcept;

template<typename Evaluate>
[[nodiscard]] Expected<Observation> Observe(Evaluate&& evaluate)noexcept(
    noexcept(Observation{})
    && noexcept(DeclVal<Observation&>().value = DeclVal<Evaluate&>()())
    && noexcept(Expected<Observation>(DeclVal<Observation&&>()))
    && noexcept(Expected<Observation>(MakeUnexpected(Failure{})))
){
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

[[nodiscard]] inline b8 EqualObservation(const Observation& lhs, const Observation& rhs)noexcept{
    return EqualBits(lhs.value, rhs.value) && lhs.flags == rhs.flags && lhs.error == rhs.error;
}

template<typename Baseline, typename Candidate, typename Additional = NoAdditionalReductionCheck>
u32 VerifyReductionCases(const StringView operation, Baseline&& baseline, Candidate&& candidate, Additional&& additional, u64& seed){
#pragma STDC FENV_ACCESS ON
    constexpr Array<u64, 14> s_Exceptional = {
        0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull,
    };
    u32 failures = 0u;
    for(usize index = 0u; index < 16384u; ++index){
        Array<u64, 8> bits = {};
        for(auto& value : bits){
            const u64 random = NextSeed(seed);
            value = index < 8192u ? s_Exceptional[random % s_Exceptional.size()] : random;
        }
        const SIMDVectorDouble lhs = VectorDoubleSetInt(bits[0], bits[1], bits[2], bits[3]), rhs = VectorDoubleSetInt(bits[4], bits[5], bits[6], bits[7]);
        const auto expected = Observe([&]()noexcept(noexcept(DeclVal<void(*)(Decay_T<decltype(baseline(lhs, rhs))>)noexcept>()(baseline(lhs, rhs)))){ return baseline(lhs, rhs); });
        if(!expected)
            return failures + 1u;
        const auto actual = Observe([&]()noexcept(noexcept(DeclVal<void(*)(Decay_T<decltype(candidate(lhs, rhs))>)noexcept>()(candidate(lhs, rhs)))){ return candidate(lhs, rhs); });
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

template<typename Verify>
VerificationSweep VerifyRoundingModes(Verify&& verify)noexcept(noexcept(DeclVal<u32&>() += DeclVal<Verify&>()(DeclVal<u64&>()))){
#pragma STDC FENV_ACCESS ON
    const auto environment = HoldFloatingPointExceptions();
    if(!environment)
        return { 1u, false };
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*environment);

    constexpr Array<i32, 4> s_Modes = { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    u64 seed = s_InitialSeed;
    u32 failures = 0u;
    for(const i32 mode : s_Modes){
        if(!SetFloatingPointRoundingMode(mode)){
            ++failures;
            break;
        }
        failures += verify(seed);
    }
    if(!SetFloatingPointEnvironment(*environment))
        ++failures;
    restoreEnvironment.release();
    return { failures, true };
}

template<usize LanesPerIteration, typename ReduceFn>
NWB_NOINLINE SIMDVectorDouble ThroughputWorkload(const Dataset& data, const usize iterations, ReduceFn&& reduce)noexcept{
    alignas(SIMDVectorDouble) Array<SIMDVectorDouble, 8> accumulators = {};
    for(usize lane = 0u; lane < 8u; ++lane)
        accumulators[lane] = VectorDoubleSet(0.125, 0.25, 0.5, 1.0);
    for(usize iteration = 0u; iteration < iterations; ++iteration){
#pragma clang loop unroll(full)
        for(usize lane = 0u; lane < LanesPerIteration; ++lane){
            const usize index = (iteration * LanesPerIteration + lane) % s_DataCount;
            accumulators[lane] = VectorDoubleAdd(accumulators[lane], reduce(LoadDouble(data.left[index]), LoadDouble(data.right[index])));
        }
    }
    SIMDVectorDouble result = accumulators[0];
    for(usize lane = 1u; lane < 8u; ++lane)
        result = VectorDoubleAdd(result, accumulators[lane]);
    return result;
}

template<typename Workload>
void Measure(const StringView operation, const b8 candidate, const b8 latency, const usize operations, Workload&& workload){
    const Timer start = TimerNow();
    const SIMDVectorDouble result = workload();
    const f64 nanoseconds = DurationInNS<f64>(TimerNow(), start);
    u64 checksum = 0u;
    for(usize lane = 0u; lane < 4u; ++lane)
        checksum ^= VectorDoubleGetIntByIndex(result, lane) + static_cast<u64>(lane);
    NWB_COUT
        << "{\"operation\":\"" << operation << "\",\"variant\":\"" << (candidate ? "candidate" : "baseline")
        << "\",\"workload\":\"" << (latency ? "latency" : "throughput") << "\",\"operations\":" << operations
        << ",\"nanoseconds\":" << nanoseconds << ",\"ns_per_operation\":" << (nanoseconds / static_cast<f64>(operations))
        << ",\"checksum\":\"" << checksum << "\"}\n"
    ;
}

template<typename CandidateWorkload, typename BaselineWorkload>
void SelectVariant(const StringView operation, const b8 candidate, const b8 latency, const usize operations, CandidateWorkload&& candidateWorkload, BaselineWorkload&& baselineWorkload){
    if(candidate)
        Measure(operation, true, latency, operations, Forward<CandidateWorkload>(candidateWorkload));
    else
        Measure(operation, false, latency, operations, Forward<BaselineWorkload>(baselineWorkload));
}

[[nodiscard]] Expected<MeasureOptions> ParseMeasureOptions(const char* const* argv, usize maximumIterations, b8 allowLatency)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

