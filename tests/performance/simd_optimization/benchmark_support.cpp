// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "benchmark_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimdOptimizationBenchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 NextSeed(u64& seed)noexcept{
    seed ^= seed << 13u;
    seed ^= seed >> 7u;
    seed ^= seed << 17u;
    return seed;
}

NWB_NOINLINE void InitializeDataset(u64 seed, Dataset& dataset)noexcept{
    const SIMDVectorDouble step = VectorDoubleReplicate(0x1p-10);
    for(usize index = 0u; index < s_DataCount; ++index){
        // Keep the seed draw order: left and right per lane, then the angle.
        Array<f64, 4> leftSteps = {};
        Array<f64, 4> rightSteps = {};
        for(usize lane = 0u; lane < 4u; ++lane){
            leftSteps[lane] = static_cast<f64>(NextSeed(seed) % 8192u);
            rightSteps[lane] = static_cast<f64>(NextSeed(seed) % 64u + 1u);
        }
        StoreDouble(VectorDoubleMultiplyAddExpression(VectorDoubleSet(leftSteps[0], leftSteps[1], leftSteps[2], leftSteps[3]), step, VectorDoubleReplicate(-4.0)), dataset.left[index]);
        StoreDouble(VectorDoubleMultiply(VectorDoubleSet(rightSteps[0], rightSteps[1], rightSteps[2], rightSteps[3]), step), dataset.right[index]);
        dataset.angles[index] = static_cast<f64>(NextSeed(seed) % 2048u + 1u) * 0x1p-10;
    }
}

b8 EqualBits(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    for(usize lane = 0u; lane < 4u; ++lane){
        if(VectorDoubleGetIntByIndex(lhs, lane) != VectorDoubleGetIntByIndex(rhs, lane))
            return false;
    }
    return true;
}

// The references use the same floating-point environment as the candidates.
SIMDVectorDouble NWB_SIMD_CALL BaselineDot2(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    const SIMDVectorDouble product = VectorDoubleMultiply(VectorDoubleAndInt(lhs, s_DoubleSIMDMaskXY), VectorDoubleAndInt(rhs, s_DoubleSIMDMaskXY));
    return VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
}

SIMDVectorDouble NWB_SIMD_CALL BaselineDot3(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    const SIMDVectorDouble product = VectorDoubleMultiply(VectorDoubleAndInt(lhs, s_DoubleSIMDMask3), VectorDoubleAndInt(rhs, s_DoubleSIMDMask3));
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    return VectorDoubleAdd(xy, VectorDoubleSplatZ(product));
}

SIMDVectorDouble NWB_SIMD_CALL BaselineDot4(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    const SIMDVectorDouble product = VectorDoubleMultiply(lhs, rhs);
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(product));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(product));
}

SIMDVectorDouble NWB_SIMD_CALL BaselineSum(const SIMDVectorDouble value)noexcept{
#pragma STDC FENV_ACCESS ON
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(value), VectorDoubleSplatY(value));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(value));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(value));
}

Expected<MeasureOptions> ParseMeasureOptions(const char* const* argv, const usize maximumIterations, const b8 allowLatency)noexcept{
    const b8 candidate = StringView(argv[3]) == "candidate";
    if(!candidate && StringView(argv[3]) != "baseline")
        return MakeUnexpected(Failure{});
    const b8 latency = StringView(argv[5]) == "latency";
    if((latency && !allowLatency) || (!latency && StringView(argv[5]) != "throughput"))
        return MakeUnexpected(Failure{});
    const auto parsedIterations = ParseU64(AStringView(argv[4]));
    if(!parsedIterations || *parsedIterations == 0u || *parsedIterations > maximumIterations)
        return MakeUnexpected(Failure{});
    return MeasureOptions{ candidate, latency, static_cast<usize>(*parsedIterations) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

