// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simdmath.h>
#include <global/timer.h>

#include <cerrno>
#include <cfenv>
#include <cstdlib>
#include <iostream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_replicated_optimization_benchmark{


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
inline constexpr usize s_DataCount = 64u;

struct Dataset{
    SIMDVectorDouble left[s_DataCount];
    SIMDVectorDouble right[s_DataCount];
    f64 angles[s_DataCount];
};

NWB_NOINLINE void InitializeDataset(Dataset& dataset, u64 seed)noexcept{
    const auto next = [&]()noexcept{
        seed ^= seed << 13u;
        seed ^= seed >> 7u;
        seed ^= seed << 17u;
        return seed;
    };
    for(usize index = 0u; index < s_DataCount; ++index){
        f64 left[4], right[4];
        for(usize lane = 0u; lane < 4u; ++lane){
            left[lane] = static_cast<f64>(next() % 8192u) * 0x1p-10 - 4.0;
            right[lane] = static_cast<f64>(next() % 64u + 1u) * 0x1p-10;
        }
        dataset.left[index] = VectorDoubleSet(left[0], left[1], left[2], left[3]);
        dataset.right[index] = VectorDoubleSet(right[0], right[1], right[2], right[3]);
        dataset.angles[index] = static_cast<f64>(next() % 2048u + 1u) * 0x1p-10;
    }
}


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
    if constexpr(Dimension == 2u) return VectorDouble2Dot(lhs, rhs);
    else if constexpr(Dimension == 3u) return VectorDouble3Dot(lhs, rhs);
    else return VectorDouble4Dot(lhs, rhs);
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
    if constexpr(Candidate) return ReplicatedACos(cosine);
    else return VectorDoubleACos(cosine);
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
        if constexpr(Dimension == 2u) return VectorDouble2Length(value);
        else if constexpr(Dimension == 3u) return VectorDouble3Length(value);
        else return VectorDouble4Length(value);
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

template<Operation::Enum Kind, bool Estimate = false>
NWB_INLINE SIMDVectorDouble ProductionValue(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Kind == Operation::Ln) return QuaternionDoubleLn(lhs);
    else if constexpr(Kind == Operation::Exp) return QuaternionDoubleExp(lhs);
    else if constexpr(Kind == Operation::Normals2){
        if constexpr(Estimate) return VectorDouble2AngleBetweenNormalsEst(lhs, rhs);
        else return VectorDouble2AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Normals3){
        if constexpr(Estimate) return VectorDouble3AngleBetweenNormalsEst(lhs, rhs);
        else return VectorDouble3AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Normals4){
        if constexpr(Estimate) return VectorDouble4AngleBetweenNormalsEst(lhs, rhs);
        else return VectorDouble4AngleBetweenNormals(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Vectors2) return VectorDouble2AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors3) return VectorDouble3AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors4) return VectorDouble4AngleBetweenVectors(lhs, rhs);
    else if constexpr(Kind == Operation::Slerp) return QuaternionDoubleSlerp(lhs, rhs, 0.375);
    else if constexpr(Kind == Operation::Length2) return VectorDouble2Length(lhs);
    else if constexpr(Kind == Operation::Length3) return VectorDouble3Length(lhs);
    else if constexpr(Kind == Operation::Length4) return VectorDouble4Length(lhs);
    else if constexpr(Kind == Operation::Reciprocal2) return VectorDouble2ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Reciprocal3) return VectorDouble3ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Reciprocal4) return VectorDouble4ReciprocalLength(lhs);
    else if constexpr(Kind == Operation::Normalize2) return VectorDouble2NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Normalize3) return VectorDouble3NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Normalize4) return VectorDouble4NormalizeOr(lhs, rhs, 0.0);
    else if constexpr(Kind == Operation::Clamp2) return VectorDouble2ClampLength(lhs, 0.5, 2.0);
    else if constexpr(Kind == Operation::Clamp3) return VectorDouble3ClampLength(lhs, 0.5, 2.0);
    else return VectorDouble4ClampLength(lhs, 0.5, 2.0);
}

template<Operation::Enum Kind, bool Estimate = false>
NWB_NOINLINE SIMDVectorDouble Production(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    return ProductionValue<Kind, Estimate>(lhs, rhs);
}

template<Operation::Enum Kind, bool Candidate>
NWB_INLINE SIMDVectorDouble Reduce(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Candidate && s_ProductionCandidate[Kind]) return ProductionValue<Kind>(lhs, rhs);
    else if constexpr(Kind == Operation::Ln) return QuaternionLn<Candidate>(lhs);
    else if constexpr(Kind == Operation::Exp) return QuaternionExp<Candidate>(lhs);
    else if constexpr(Kind == Operation::Normals2) return Angle<2u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normals3) return Angle<3u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normals4) return Angle<4u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors2) return Angle<2u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors3) return Angle<3u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Vectors4) return Angle<4u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Slerp) return QuaternionSlerp<Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Length2) return Length<2u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Length3) return Length<3u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Length4) return Length<4u, false, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal2) return Length<2u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal3) return Length<3u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Reciprocal4) return Length<4u, true, Candidate>(lhs);
    else if constexpr(Kind == Operation::Normalize2) return ControlledLength<2u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normalize3) return ControlledLength<3u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Normalize4) return ControlledLength<4u, false, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Clamp2) return ControlledLength<2u, true, Candidate>(lhs, rhs);
    else if constexpr(Kind == Operation::Clamp3) return ControlledLength<3u, true, Candidate>(lhs, rhs);
    else return ControlledLength<4u, true, Candidate>(lhs, rhs);
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDVectorDouble Evaluate(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Candidate && s_ProductionCandidate[Kind]) return Production<Kind>(lhs, rhs);
    else return Reduce<Kind, Candidate>(lhs, rhs);
}

NWB_INLINE bool EqualBits(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    for(usize lane = 0u; lane < 4u; ++lane){
        if(VectorDoubleGetIntByIndex(lhs, lane) != VectorDoubleGetIntByIndex(rhs, lane))
            return false;
    }
    return true;
}

template<Operation::Enum Kind>
u32 VerifyReduction(u64& seed)noexcept{
#pragma STDC FENV_ACCESS ON
    constexpr u64 exceptional[]{ 0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull };
    u32 failures = 0u;
    for(usize index = 0u; index < 16384u; ++index){
        u64 bits[8];
        for(auto& value : bits){
            seed ^= seed << 13u;
            seed ^= seed >> 7u;
            seed ^= seed << 17u;
            value = index < 8192u ? exceptional[seed % LengthOf(exceptional)] : seed;
        }
        const SIMDVectorDouble lhs = VectorDoubleSetInt(bits[0], bits[1], bits[2], bits[3]), rhs = VectorDoubleSetInt(bits[4], bits[5], bits[6], bits[7]);
        if(feclearexcept(FE_ALL_EXCEPT) != 0)
            return failures + 1u;
        errno = E2BIG;
        const SIMDVectorDouble expected = Evaluate<Kind, false>(lhs, rhs);
        const i32 expectedFlags = fetestexcept(FE_ALL_EXCEPT), expectedErrno = errno;
        if(feclearexcept(FE_ALL_EXCEPT) != 0)
            return failures + 1u;
        errno = E2BIG;
        const SIMDVectorDouble actual = Evaluate<Kind, true>(lhs, rhs);
        const i32 actualFlags = fetestexcept(FE_ALL_EXCEPT), actualErrno = errno;
        if(feclearexcept(FE_ALL_EXCEPT) != 0)
            return failures + 1u;
        errno = E2BIG;
        const SIMDVectorDouble production = Production<Kind>(lhs, rhs);
        const i32 productionFlags = fetestexcept(FE_ALL_EXCEPT), productionErrno = errno;
        const SIMDVectorDouble productionExpected = s_ProductionCandidate[Kind] ? actual : expected;
        const i32 productionExpectedFlags = s_ProductionCandidate[Kind] ? actualFlags : expectedFlags;
        const i32 productionExpectedErrno = s_ProductionCandidate[Kind] ? actualErrno : expectedErrno;
        if(!EqualBits(productionExpected, production) || productionExpectedFlags != productionFlags || productionExpectedErrno != productionErrno){
            if(++failures <= 3u)
                NWB_COUT << "production mismatch operation=" << s_OperationNames[Kind] << " case=" << index << '\n';
        }
        if constexpr(Kind == Operation::Normals2 || Kind == Operation::Normals3 || Kind == Operation::Normals4){
            if(feclearexcept(FE_ALL_EXCEPT) != 0)
                return failures + 1u;
            errno = E2BIG;
            const SIMDVectorDouble estimate = Production<Kind, true>(lhs, rhs);
            const i32 estimateFlags = fetestexcept(FE_ALL_EXCEPT), estimateErrno = errno;
            if(!EqualBits(productionExpected, estimate) || productionExpectedFlags != estimateFlags || productionExpectedErrno != estimateErrno)
                ++failures;
        }
        if(!EqualBits(expected, actual) || expectedFlags != actualFlags || expectedErrno != actualErrno){
            if(++failures <= 3u)
                NWB_COUT << "reduction mismatch operation=" << s_OperationNames[Kind] << " case=" << index << " flags=" << expectedFlags << '/' << actualFlags << " errno=" << expectedErrno << '/' << actualErrno << '\n';
        }
    }
    return failures;
}

u32 Verify()noexcept{
#pragma STDC FENV_ACCESS ON
    fenv_t environment{};
    if(feholdexcept(&environment) != 0)
        return 1u;
    constexpr i32 modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    u64 seed = 0x8923409102431291ull;
    u32 failures = 0u;
    for(const i32 mode : modes){
        if(fesetround(mode) != 0){
            ++failures;
            break;
        }
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
    }
    if(fesetenv(&environment) != 0)
        ++failures;
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
            accumulators[lane] = VectorDoubleAdd(accumulators[lane], Reduce<Kind, Candidate>(data.left[index], data.right[index]));
        }
    }
    SIMDVectorDouble result = accumulators[0];
    for(usize lane = 1u; lane < 8u; ++lane)
        result = VectorDoubleAdd(result, accumulators[lane]);
    return result;
}

template<Operation::Enum Kind, bool Candidate>
void Measure(const Dataset& data, const usize iterations, const bool latency){
    const Timer start = TimerNow();
    const SIMDVectorDouble result = Workload<Kind, Candidate>(data, iterations, latency);
    const f64 nanoseconds = DurationInNS<f64>(TimerNow(), start);
    u64 checksum = 0u;
    for(usize lane = 0u; lane < 4u; ++lane)
        checksum ^= VectorDoubleGetIntByIndex(result, lane) + static_cast<u64>(lane);
    const usize operations = iterations * 4u;
    NWB_COUT
        << "{\"operation\":\"" << s_OperationNames[Kind] << "\",\"variant\":\"" << (Candidate ? "candidate" : "baseline")
        << "\",\"workload\":\"" << (latency ? "latency" : "throughput") << "\",\"operations\":" << operations
        << ",\"nanoseconds\":" << nanoseconds << ",\"ns_per_operation\":" << nanoseconds / static_cast<f64>(operations)
        << ",\"checksum\":\"" << checksum << "\"}\n"
    ;
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
    using namespace __hidden_replicated_optimization_benchmark;
    NWB_COUT.precision(17);
    if(argc == 2 && StringView(argv[1]) == "--verify")
        return Verify() == 0u ? 0 : 1;
    if(argc != 6 || StringView(argv[1]) != "--measure"){
        NWB_CERR << "usage: replicated_benchmark --verify | --measure operation baseline|candidate iterations throughput\n";
        return 2;
    }
    const bool candidate = StringView(argv[3]) == "candidate";
    if(!candidate && StringView(argv[3]) != "baseline")
        return 2;
    const bool latency = StringView(argv[5]) == "latency";
    if(latency || StringView(argv[5]) != "throughput")
        return 2;
    char* end = nullptr;
    const usize iterations = static_cast<usize>(strtoull(argv[4], &end, 10));
    if(iterations == 0u || end == argv[4] || *end != '\0')
        return 2;
    Dataset data{};
    InitializeDataset(data, 0x8923409102431291ull);
    for(usize index = 0u; index < s_DataCount; ++index){
        if(StringView(argv[2]) == "normals2"){
            data.left[index] = VectorDouble2Normalize(data.left[index]);
            data.right[index] = VectorDouble2Normalize(data.right[index]);
        }
        else if(StringView(argv[2]) == "normals3"){
            data.left[index] = VectorDouble3Normalize(data.left[index]);
            data.right[index] = VectorDouble3Normalize(data.right[index]);
        }
        else if(StringView(argv[2]) == "normals4" || StringView(argv[2]) == "ln" || StringView(argv[2]) == "slerp"){
            data.left[index] = VectorDouble4Normalize(data.left[index]);
            data.right[index] = VectorDouble4Normalize(data.right[index]);
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

