// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "benchmark_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_replicated_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Operation{
enum Enum : u32{
    Ln,
    Exp,
    Normals2,
    Normals3,
    Normals4,
    Vectors2,
    Vectors3,
    Vectors4,
    Slerp,
    Length2,
    Length3,
    Length4,
    Reciprocal2,
    Reciprocal3,
    Reciprocal4,
    Normalize2,
    Normalize3,
    Normalize4,
    Clamp2,
    Clamp3,
    Clamp4,
    Count
};
};

inline constexpr StringView s_OperationNames[]{
    "ln", "exp", "normals2", "normals3", "normals4", "vectors2", "vectors3", "vectors4", "slerp",
    "length2", "length3", "length4", "reciprocal2", "reciprocal3", "reciprocal4", "normalize2", "normalize3", "normalize4", "clamp2", "clamp3", "clamp4"
};
using SimdOptimizationBenchmark::s_DataCount;
using SimdOptimizationBenchmark::Dataset;
using SimdOptimizationBenchmark::InitializeDataset;
using SimdOptimizationBenchmark::EqualBits;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble ReplicatedACos(const SIMDVectorDouble value)noexcept{
    return VectorDoubleReplicate(ACos(VectorDoubleGetX(value)));
}

NWB_INLINE SIMDVectorDouble ReplicatedSin(const SIMDVectorDouble value)noexcept{
    return VectorDoubleReplicate(Sin(VectorDoubleGetX(value)));
}

NWB_INLINE SIMDVectorDouble ReplicatedATan2(const SIMDVectorDouble y, const SIMDVectorDouble x)noexcept{
    return VectorDoubleReplicate(ATan2(VectorDoubleGetX(y), VectorDoubleGetX(x)));
}

template<u32 Dimension>
NWB_INLINE SIMDVectorDouble Dot(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Dimension == 2u)
        return VectorDouble2Dot(lhs, rhs);
    else if constexpr(Dimension == 3u)
        return VectorDouble3Dot(lhs, rhs);
    else
        return VectorDouble4Dot(lhs, rhs);
}

template<u32 Dimension>
NWB_INLINE SIMDVectorDouble ReciprocalLength(const SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(Dot<Dimension>(value, value));
}

template<u32 Dimension, bool Vectors, bool Candidate>
NWB_INLINE SIMDVectorDouble Angle(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    SIMDVectorDouble cosine = Dot<Dimension>(lhs, rhs);
    if constexpr(Vectors)
        cosine = VectorDoubleMultiply(cosine, VectorDoubleMultiply(ReciprocalLength<Dimension>(lhs), ReciprocalLength<Dimension>(rhs)));
    cosine = VectorDoubleClamp(cosine, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    if constexpr(Candidate)
        return ReplicatedACos(cosine);
    else
        return VectorDoubleACos(cosine);
}

template<bool Candidate>
NWB_INLINE SIMDVectorDouble QuaternionLn(const SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble bound = VectorDoubleReplicate(1.0 - SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon);
    const SIMDVectorDouble w = VectorDoubleSplatW(value), xyz = VectorDoubleAndInt(value, s_DoubleSIMDMask3);
    const SIMDVectorDouble control = VectorDoubleInBounds(w, bound);
    const SIMDVectorDouble theta = Candidate ? ReplicatedACos(w) : VectorDoubleACos(w);
    const SIMDVectorDouble sine = Candidate ? ReplicatedSin(theta) : VectorDoubleSin(theta);
    return VectorDoubleSelect(xyz, VectorDoubleMultiply(xyz, VectorDoubleDivide(theta, sine)), control);
}

template<bool Candidate>
NWB_INLINE SIMDVectorDouble QuaternionExp(const SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble theta = VectorDouble3Length(value);
    SIMDVectorDouble sine{}, cosine{};
    if constexpr(Candidate)
        SIMDVectorDoubleDetail::SinCosReplicated(sine, cosine, theta);
    else
        VectorDoubleSinCos(sine, cosine, theta);
    const SIMDVectorDouble scale = VectorDoubleDivide(sine, theta);
    SIMDVectorDouble result = VectorDoubleMultiply(value, scale);
    result = VectorDoubleSelect(result, value, VectorDoubleNearEqual(theta, VectorDoubleZero(), s_DoubleSIMDEpsilon));
    return VectorDoubleSelect(cosine, result, s_DoubleSIMDSelect1110);
}

template<bool Candidate>
NWB_INLINE SIMDVectorDouble QuaternionSlerp(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    const SIMDVectorDouble t = VectorDoubleReplicate(0.375);
    const SIMDVectorDouble bound = VectorDoubleReplicate(1.0 - SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon);
    SIMDVectorDouble cosine = QuaternionDoubleDot(lhs, rhs);
    SIMDVectorDouble control = VectorDoubleLess(cosine, VectorDoubleZero());
    const SIMDVectorDouble sign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, control);
    cosine = VectorDoubleMultiply(cosine, sign);
    control = VectorDoubleLess(cosine, bound);
    const SIMDVectorDouble sine = VectorDoubleSqrt(VectorDoubleNegativeMultiplySubtract(cosine, cosine, s_DoubleSIMDOne));
    const SIMDVectorDouble omega = Candidate ? ReplicatedATan2(sine, cosine) : VectorDoubleATan2(sine, cosine);
    SIMDVectorDouble signMask = VectorDoubleSplatSignMask();
    SIMDVectorDouble weights = VectorDoubleShiftLeft(t, VectorDoubleZero(), 2u);
    signMask = VectorDoubleShiftLeft(signMask, VectorDoubleZero(), 3u);
    weights = VectorDoubleXorInt(weights, signMask);
    weights = VectorDoubleAdd(s_DoubleSIMDIdentityR0, weights);
    // Weighted arguments are nonuniform; retain the ordinary four-lane sine path.
    SIMDVectorDouble firstWeight = VectorDoubleMultiply(weights, omega);
    firstWeight = VectorDoubleSin(firstWeight);
    firstWeight = VectorDoubleMultiply(firstWeight, VectorDoubleReciprocal(sine));
    firstWeight = VectorDoubleSelect(weights, firstWeight, control);
    const SIMDVectorDouble secondWeight = VectorDoubleMultiply(VectorDoubleSplatY(firstWeight), sign);
    firstWeight = VectorDoubleSplatX(firstWeight);
    return VectorDoubleMultiplyAdd(rhs, secondWeight, VectorDoubleMultiply(lhs, firstWeight));
}

template<u32 Dimension, bool Reciprocal, bool Candidate>
NWB_INLINE SIMDVectorDouble Length(const SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2)
    if constexpr(Candidate){
        const __m128d input = SIMDVectorDoubleDetail::GetLow(Dot<Dimension>(value, value));
        __m128d root = _mm_sqrt_sd(input, input);
        if constexpr(Reciprocal)
            root = _mm_div_sd(_mm_set_sd(1.0), root);
        const __m128d result = _mm_unpacklo_pd(root, root);
        return SIMDVectorDoubleDetail::FromPairs(result, result);
    }
    else
#endif
    if constexpr(Reciprocal)
        return ReciprocalLength<Dimension>(value);
    else{
        if constexpr(Dimension == 2u)
            return VectorDouble2Length(value);
        else if constexpr(Dimension == 3u)
            return VectorDouble3Length(value);
        else
            return VectorDouble4Length(value);
    }
}

template<u32 Dimension, bool Clamp, bool Candidate>
NWB_INLINE SIMDVectorDouble ControlledLength(const SIMDVectorDouble value, const SIMDVectorDouble fallback)noexcept{
    const SIMDVectorDouble lengthSquared = Dot<Dimension>(value, value);
    const auto reciprocalLength = [&]()noexcept{
#if defined(NWB_HAS_SSE2) && !defined(NWB_HAS_AVX)
        if constexpr(Candidate)
            return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(lengthSquared);
        else
#endif
            return VectorDoubleReciprocalSqrt(lengthSquared);
    };
    const SIMDVectorDouble mask = Dimension == 2u ? SIMDVectorDouble(s_DoubleSIMDMaskXY) : Dimension == 3u ? SIMDVectorDouble(s_DoubleSIMDMask3) : VectorDoubleTrueInt();
    if constexpr(Clamp){
        const SIMDVectorDouble reciprocal = reciprocalLength();
        const SIMDVectorDouble minimum = VectorDoubleReplicate(0.5), maximum = VectorDoubleReplicate(2.0);
        const SIMDVectorDouble infinite = VectorDoubleEqualInt(lengthSquared, s_DoubleSIMDInfinity), zero = VectorDoubleEqual(lengthSquared, VectorDoubleZero());
        const SIMDVectorDouble select = VectorDoubleEqualInt(infinite, zero);
        SIMDVectorDouble normal = VectorDoubleMultiply(value, reciprocal), length = VectorDoubleMultiply(lengthSquared, reciprocal);
        length = VectorDoubleSelect(lengthSquared, length, select);
        normal = VectorDoubleSelect(lengthSquared, normal, select);
        const SIMDVectorDouble above = VectorDoubleGreater(length, maximum), below = VectorDoubleLess(length, minimum);
        SIMDVectorDouble clamped = VectorDoubleSelect(length, maximum, above);
        clamped = VectorDoubleSelect(clamped, minimum, below);
        const SIMDVectorDouble result = VectorDoubleMultiply(normal, clamped);
        return VectorDoubleAndInt(VectorDoubleSelect(result, value, VectorDoubleEqualInt(above, below)), mask);
    }
    else{
        const SIMDVectorDouble finiteLength = VectorDoubleEqualInt(VectorDoubleOrInt(VectorDoubleIsNaN(lengthSquared), VectorDoubleIsInfinite(lengthSquared)), VectorDoubleFalseInt());
        const SIMDVectorDouble validLength = VectorDoubleAndInt(VectorDoubleGreater(lengthSquared, VectorDoubleZero()), finiteLength);
        const SIMDVectorDouble reciprocal = reciprocalLength();
        const SIMDVectorDouble normalized = VectorDoubleAndInt(VectorDoubleMultiply(value, reciprocal), mask);
        const SIMDVectorDouble validNormalized = VectorDoubleEqualInt(VectorDoubleOrInt(VectorDoubleIsNaN(normalized), VectorDoubleIsInfinite(normalized)), VectorDoubleFalseInt());
        return VectorDoubleSelect(VectorDoubleAndInt(fallback, mask), normalized, VectorDoubleAndInt(validLength, validNormalized));
    }
}

// Switch only accepted candidates to production after their implementations are applied.
inline constexpr bool s_ProductionCandidate[Operation::Count]{ true, true, true, true, true, true, true, true, true, false, false, false, true, true, true, false, false, false, false, false, false };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<Operation::Enum Kind, bool Estimate = false>
NWB_INLINE SIMDVectorDouble ProductionValue(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Kind == Operation::Ln)
        return QuaternionDoubleLn(lhs);
    else if constexpr(Kind == Operation::Exp)
        return QuaternionDoubleExp(lhs);
    else if constexpr(Kind == Operation::Normals2){
        if constexpr(Estimate)
            return VectorDouble2AngleBetweenNormalsEst(lhs, rhs);
        else
            return VectorDouble2AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Normals3){
        if constexpr(Estimate)
            return VectorDouble3AngleBetweenNormalsEst(lhs, rhs);
        else
            return VectorDouble3AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Normals4){
        if constexpr(Estimate)
            return VectorDouble4AngleBetweenNormalsEst(lhs, rhs);
        else
            return VectorDouble4AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Vectors2)
        return VectorDouble2AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors3)
        return VectorDouble3AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors4)
        return VectorDouble4AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Slerp)
        return QuaternionDoubleSlerp(lhs, rhs, 0.375);
    else if constexpr(Kind == Operation::Length2)
        return VectorDouble2Length(lhs);
    else if constexpr(Kind == Operation::Length3)
        return VectorDouble3Length(lhs);
    else if constexpr(Kind == Operation::Length4)
        return VectorDouble4Length(lhs);
    else if constexpr(Kind == Operation::Reciprocal2)
        return VectorDouble2ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Reciprocal3)
        return VectorDouble3ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Reciprocal4)
        return VectorDouble4ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Normalize2)
        return VectorDouble2NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Normalize3)
        return VectorDouble3NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Normalize4)
        return VectorDouble4NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Clamp2)
        return VectorDouble2ClampLength(lhs, 0.5, 2.0);
    else if constexpr(Kind == Operation::Clamp3)
        return VectorDouble3ClampLength(lhs, 0.5, 2.0);
    else
        return VectorDouble4ClampLength(lhs, 0.5, 2.0);
}

template<Operation::Enum Kind, bool Estimate = false>
NWB_NOINLINE SIMDVectorDouble Production(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    return ProductionValue<Kind, Estimate>(lhs, rhs);
}

template<Operation::Enum Kind, bool Candidate>
NWB_INLINE SIMDVectorDouble Reduce(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Candidate && s_ProductionCandidate[Kind])
        return ProductionValue<Kind>(lhs, rhs);
    else if constexpr(Kind == Operation::Ln)
        return QuaternionLn<Candidate>(lhs);
    else if constexpr(Kind == Operation::Exp)
        return QuaternionExp<Candidate>(lhs);
    else if constexpr(Kind == Operation::Normals2)
        return Angle<2u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normals3)
        return Angle<3u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normals4)
        return Angle<4u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors2)
        return Angle<2u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors3)
        return Angle<3u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors4)
        return Angle<4u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Slerp)
        return QuaternionSlerp<Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Length2)
        return Length<2u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Length3)
        return Length<3u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Length4)
        return Length<4u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal2)
        return Length<2u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal3)
        return Length<3u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal4)
        return Length<4u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Normalize2)
        return ControlledLength<2u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normalize3)
        return ControlledLength<3u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normalize4)
        return ControlledLength<4u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Clamp2)
        return ControlledLength<2u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Clamp3)
        return ControlledLength<3u, true, Candidate>(lhs, rhs);
    else
        return ControlledLength<4u, true, Candidate>(lhs, rhs);
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDVectorDouble Evaluate(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Candidate && s_ProductionCandidate[Kind])
        return Production<Kind>(lhs, rhs);
    else
        return Reduce<Kind, Candidate>(lhs, rhs);
}

template<Operation::Enum Kind>
u32 VerifyReduction(u64& seed){
#pragma STDC FENV_ACCESS ON
    return SimdOptimizationBenchmark::VerifyReductionCases(
        seed, s_OperationNames[Kind],
        [](const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{ return Evaluate<Kind, false>(lhs, rhs); },
        [](const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{ return Evaluate<Kind, true>(lhs, rhs); },
        [](const SIMDVectorDouble lhs, const SIMDVectorDouble rhs, const SimdOptimizationBenchmark::Observation& expected, const SimdOptimizationBenchmark::Observation& actual, const usize index, u32& failures){
            const auto production = SimdOptimizationBenchmark::Observe([&]()noexcept{ return Production<Kind>(lhs, rhs); });
            if(!production)
                return false;
            const auto& productionExpected = s_ProductionCandidate[Kind] ? actual : expected;
            if(!SimdOptimizationBenchmark::EqualObservation(productionExpected, *production)){
                if(++failures <= 3u)
                    NWB_COUT << "production mismatch operation=" << s_OperationNames[Kind] << " case=" << index << '\n';
            }
            if constexpr(Kind == Operation::Normals2 || Kind == Operation::Normals3 || Kind == Operation::Normals4){
                const auto estimate = SimdOptimizationBenchmark::Observe([&]()noexcept{ return Production<Kind, true>(lhs, rhs); });
                if(!estimate)
                    return false;
                if(!SimdOptimizationBenchmark::EqualObservation(productionExpected, *estimate))
                    ++failures;
            }
            return true;
        }
    );
}

u32 Verify(){
#pragma STDC FENV_ACCESS ON
    const auto verification = SimdOptimizationBenchmark::VerifyRoundingModes([](u64& seed){
        u32 failures = 0u;
        failures += VerifyReduction<Operation::Ln>(seed);
        failures += VerifyReduction<Operation::Exp>(seed);
        failures += VerifyReduction<Operation::Normals2>(seed);
        failures += VerifyReduction<Operation::Normals3>(seed);
        failures += VerifyReduction<Operation::Normals4>(seed);
        failures += VerifyReduction<Operation::Vectors2>(seed);
        failures += VerifyReduction<Operation::Vectors3>(seed);
        failures += VerifyReduction<Operation::Vectors4>(seed);
        failures += VerifyReduction<Operation::Slerp>(seed);
        failures += VerifyReduction<Operation::Length2>(seed);
        failures += VerifyReduction<Operation::Length3>(seed);
        failures += VerifyReduction<Operation::Length4>(seed);
        failures += VerifyReduction<Operation::Reciprocal2>(seed);
        failures += VerifyReduction<Operation::Reciprocal3>(seed);
        failures += VerifyReduction<Operation::Reciprocal4>(seed);
        failures += VerifyReduction<Operation::Normalize2>(seed);
        failures += VerifyReduction<Operation::Normalize3>(seed);
        failures += VerifyReduction<Operation::Normalize4>(seed);
        failures += VerifyReduction<Operation::Clamp2>(seed);
        failures += VerifyReduction<Operation::Clamp3>(seed);
        failures += VerifyReduction<Operation::Clamp4>(seed);
        return failures;
    }
    );
    if(!verification.environmentAvailable)
        return verification.failures;
    const u32 failures = verification.failures;
    NWB_COUT << "verification failures=" << failures << " vector_cases=1376256\n";
    return failures;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDVectorDouble Workload(const Dataset& data, const usize iterations, const bool)noexcept{
    SIMDVectorDouble accumulators[8];
    for(usize lane = 0u; lane < 8u; ++lane)
        accumulators[lane] = VectorDoubleSet(0.125, 0.25, 0.5, 1.0);
    for(usize iteration = 0u; iteration < iterations; ++iteration){
#pragma clang loop unroll(full)
        for(usize lane = 0u; lane < 4u; ++lane){
            const usize index = (iteration * 4u + lane) % s_DataCount;
            accumulators[lane] = VectorDoubleAdd(accumulators[lane], Reduce<Kind, Candidate>(LoadDouble(data.left[index]), LoadDouble(data.right[index])));
        }
    }
    SIMDVectorDouble result = accumulators[0];
    for(usize lane = 1u; lane < 8u; ++lane)
        result = VectorDoubleAdd(result, accumulators[lane]);
    return result;
}

template<Operation::Enum Kind, bool Candidate>
void Measure(const Dataset& data, const usize iterations, const bool latency){
    const usize operations = iterations * 4u;
    SimdOptimizationBenchmark::Measure(s_OperationNames[Kind], Candidate, latency, operations, [&]()noexcept{ return Workload<Kind, Candidate>(data, iterations, latency); });
}

template<Operation::Enum Kind>
void SelectVariant(const Dataset& data, const bool candidate, const usize iterations, const bool latency){
    if(candidate)
        Measure<Kind, true>(data, iterations, latency);
    else
        Measure<Kind, false>(data, iterations, latency);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(const int argc, const char* const* argv){
    using namespace __hidden_replicated_benchmark;
    NWB_COUT.precision(17);
    if(argc == 2 && StringView(argv[1]) == "--verify")
        return Verify() == 0u ? 0 : 1;
    if(argc != 6 || StringView(argv[1]) != "--measure"){
        NWB_CERR << "usage: replicated_benchmark --verify | --measure operation baseline|candidate iterations throughput\n";
        return 2;
    }
    const auto options = SimdOptimizationBenchmark::ParseMeasureOptions(argv, Limit<usize>::s_Max / 4u, false);
    if(!options)
        return 2;
    const bool candidate = options->candidate, latency = options->latency;
    const usize iterations = options->iterations;
    Dataset data{};
    InitializeDataset(data, SimdOptimizationBenchmark::s_InitialSeed);
    for(usize index = 0u; index < s_DataCount; ++index){
        if(StringView(argv[2]) == "normals2"){
            StoreDouble(VectorDouble2Normalize(LoadDouble(data.left[index])), data.left[index]);
            StoreDouble(VectorDouble2Normalize(LoadDouble(data.right[index])), data.right[index]);
        }
        else if(StringView(argv[2]) == "normals3"){
            StoreDouble(VectorDouble3Normalize(LoadDouble(data.left[index])), data.left[index]);
            StoreDouble(VectorDouble3Normalize(LoadDouble(data.right[index])), data.right[index]);
        }
        else if(StringView(argv[2]) == "normals4" || StringView(argv[2]) == "ln" || StringView(argv[2]) == "slerp"){
            StoreDouble(VectorDouble4Normalize(LoadDouble(data.left[index])), data.left[index]);
            StoreDouble(VectorDouble4Normalize(LoadDouble(data.right[index])), data.right[index]);
        }
    }
    for(u32 index = 0u; index < Operation::Count; ++index){
        if(StringView(argv[2]) != s_OperationNames[index])
            continue;
        switch(index){
        case Operation::Ln: SelectVariant<Operation::Ln>(data, candidate, iterations, latency); break;
        case Operation::Exp: SelectVariant<Operation::Exp>(data, candidate, iterations, latency); break;
        case Operation::Normals2: SelectVariant<Operation::Normals2>(data, candidate, iterations, latency); break;
        case Operation::Normals3: SelectVariant<Operation::Normals3>(data, candidate, iterations, latency); break;
        case Operation::Normals4: SelectVariant<Operation::Normals4>(data, candidate, iterations, latency); break;
        case Operation::Vectors2: SelectVariant<Operation::Vectors2>(data, candidate, iterations, latency); break;
        case Operation::Vectors3: SelectVariant<Operation::Vectors3>(data, candidate, iterations, latency); break;
        case Operation::Vectors4: SelectVariant<Operation::Vectors4>(data, candidate, iterations, latency); break;
        case Operation::Slerp: SelectVariant<Operation::Slerp>(data, candidate, iterations, latency); break;
        case Operation::Length2: SelectVariant<Operation::Length2>(data, candidate, iterations, latency); break;
        case Operation::Length3: SelectVariant<Operation::Length3>(data, candidate, iterations, latency); break;
        case Operation::Length4: SelectVariant<Operation::Length4>(data, candidate, iterations, latency); break;
        case Operation::Reciprocal2: SelectVariant<Operation::Reciprocal2>(data, candidate, iterations, latency); break;
        case Operation::Reciprocal3: SelectVariant<Operation::Reciprocal3>(data, candidate, iterations, latency); break;
        case Operation::Reciprocal4: SelectVariant<Operation::Reciprocal4>(data, candidate, iterations, latency); break;
        case Operation::Normalize2: SelectVariant<Operation::Normalize2>(data, candidate, iterations, latency); break;
        case Operation::Normalize3: SelectVariant<Operation::Normalize3>(data, candidate, iterations, latency); break;
        case Operation::Normalize4: SelectVariant<Operation::Normalize4>(data, candidate, iterations, latency); break;
        case Operation::Clamp2: SelectVariant<Operation::Clamp2>(data, candidate, iterations, latency); break;
        case Operation::Clamp3: SelectVariant<Operation::Clamp3>(data, candidate, iterations, latency); break;
        case Operation::Clamp4: SelectVariant<Operation::Clamp4>(data, candidate, iterations, latency); break;
        default: return 2;
        }
        return 0;
    }
    return 2;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

