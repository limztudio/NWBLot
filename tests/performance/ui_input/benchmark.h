// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/toolkit/input/router.h>

#include <global/expected.h>
#include <global/simd/vector_arithmetic.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_SkinPath = "tests/performance/ui_input/skin";

[[nodiscard]] inline u64 MixChecksum(const u64 hash, const u64 value)noexcept{
    return (hash ^ value) * 1099511628211u;
}

template<typename Operation, typename Checksum>
[[nodiscard]] bool Measure(const AStringView name, const u64 setupChecksum, Operation&& operation, Checksum&& checksum){
    constexpr usize s_Samples = 11u;
    constexpr usize s_MaxIterations = 1u << 24u;
    constexpr f64 s_MinSampleNS = 20000000.0;
    for(usize warmup = 0u; warmup < 32u; ++warmup){
        if(!operation())
            return false;
    }
    const Expected<u64> behaviorChecksum = checksum();
    if(!behaviorChecksum)
        return false;
    const auto batch = [&](const usize iterations) -> Expected<f64>{
        const Timer begin = TimerNow();
        for(usize iteration = 0u; iteration < iterations; ++iteration){
            if(!operation())
                return MakeUnexpected(Failure{});
        }
        const f64 elapsed = DurationInNS<f64>(TimerNow(), begin);
        const Expected<u64> batchChecksum = checksum();
        if(!batchChecksum || *batchChecksum != *behaviorChecksum)
            return MakeUnexpected(Failure{});
        return elapsed;
    };
    usize iterations = 1u;
    while(true){
        const auto elapsed = batch(iterations);
        if(!elapsed)
            return false;
        if(*elapsed >= s_MinSampleNS)
            break;
        if(iterations == s_MaxIterations)
            return false;
        iterations *= 2u;
    }
    Array<f64, s_Samples> samples{};
    for(f64& sample : samples){
        const auto elapsed = batch(iterations);
        if(!elapsed)
            return false;
        sample = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(*elapsed), VectorDoubleReplicate(static_cast<f64>(iterations))));
    }
    auto sorted = samples;
    Sort(sorted.begin(), sorted.end());
    const auto oldPrecision = NWB_COUT.precision(17);
    NWB_COUT << "{\"type\":\"ui_input_benchmark\",\"name\":\"" << name << "\",\"iterationsPerSample\":" << iterations
        << ",\"setupChecksum\":" << setupChecksum << ",\"behaviorChecksum\":" << *behaviorChecksum << ",\"medianNsPerOperation\":" << sorted[s_Samples / 2u] << ",\"samplesNsPerOperation\":[";
    for(usize index = 0u; index < samples.size(); ++index){
        if(index != 0u)
            NWB_COUT << ',';
        NWB_COUT << samples[index];
    }
    NWB_COUT << "]}\n";
    NWB_COUT.precision(oldPrecision);
    return true;
}

[[nodiscard]] bool RunPointerBenchmarks();
[[nodiscard]] bool RunPaintBenchmarks();
[[nodiscard]] bool RunEditBenchmarks();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

