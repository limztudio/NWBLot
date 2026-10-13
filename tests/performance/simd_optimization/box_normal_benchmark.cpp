// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/scope_exit.h>
#include <global/timer.h>
#include <global/text_utils.h>

#include <cerrno>
#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_box_normal_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Scalar>
struct Case{
    Conditional_T<IsSame_V<Scalar, f64>, SIMDVectorDouble, SIMDVectorSingle> position;
    Conditional_T<IsSame_V<Scalar, f64>, SIMDVectorDouble, SIMDVectorSingle> extents;
    Conditional_T<IsSame_V<Scalar, f64>, SIMDVectorDouble, SIMDVectorSingle> fallback;
    Scalar threshold;
};

inline constexpr usize s_CaseCount = 32768u;
inline constexpr usize s_DataCount = 128u;
volatile u64 s_Checksum = 0u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_NOINLINE SIMDVectorSingle NWB_SIMD_CALL OriginalSingle(const SIMDVectorSingle position, const SIMDVectorSingle halfExtents, const SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVectorSingle outside = VectorMax(q, VectorZero());
    const SIMDVectorSingle signedOutside = VectorSelect(VectorNegate(outside), outside, VectorGreaterOrEqual(position, VectorZero()));
    const SIMDVectorSingle outsideNormal = Vector3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVectorSingle useOutside = VectorGreater(Vector3LengthSq(outside), VectorReplicate(minLengthSquared));

    const SIMDVectorSingle qX = VectorSplatX(q);
    const SIMDVectorSingle qY = VectorSplatY(q);
    const SIMDVectorSingle qZ = VectorSplatZ(q);
    const SIMDVectorSingle xIsMax = VectorAndInt(VectorGreaterOrEqual(qX, qY), VectorGreaterOrEqual(qX, qZ));
    const SIMDVectorSingle yIsMax = VectorGreaterOrEqual(qY, qZ);
    const SIMDVectorSingle xNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskX);
    const SIMDVectorSingle yNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskY);
    const SIMDVectorSingle zNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskZ);
    SIMDVectorSingle faceNormal = VectorSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorSelect(faceNormal, xNormal, xIsMax);
    return VectorSelect(faceNormal, outsideNormal, useOutside);
}

[[nodiscard]] NWB_NOINLINE SIMDVectorDouble NWB_SIMD_CALL OriginalDouble(const SIMDVectorDouble position, const SIMDVectorDouble halfExtents, const SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble q = VectorDoubleSubtract(VectorDoubleAbs(position), halfExtents);
    const SIMDVectorDouble outside = VectorDoubleMax(q, VectorDoubleZero());
    const SIMDVectorDouble signedOutside = VectorDoubleSelect(VectorDoubleNegate(outside), outside, VectorDoubleGreaterOrEqual(position, VectorDoubleZero()));
    const SIMDVectorDouble outsideNormal = VectorDouble3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVectorDouble useOutside = VectorDoubleGreater(VectorDouble3LengthSq(outside), VectorDoubleReplicate(minLengthSquared));

    const SIMDVectorDouble qX = VectorDoubleSplatX(q);
    const SIMDVectorDouble qY = VectorDoubleSplatY(q);
    const SIMDVectorDouble qZ = VectorDoubleSplatZ(q);
    const SIMDVectorDouble xIsMax = VectorDoubleAndInt(VectorDoubleGreaterOrEqual(qX, qY), VectorDoubleGreaterOrEqual(qX, qZ));
    const SIMDVectorDouble yIsMax = VectorDoubleGreaterOrEqual(qY, qZ);
    const SIMDVectorDouble xNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskX);
    const SIMDVectorDouble yNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskY);
    const SIMDVectorDouble zNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskZ);
    SIMDVectorDouble faceNormal = VectorDoubleSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorDoubleSelect(faceNormal, xNormal, xIsMax);
    return VectorDoubleSelect(faceNormal, outsideNormal, useOutside);
}

[[nodiscard]] NWB_NOINLINE SIMDVectorSingle NWB_SIMD_CALL CandidateSingle(const SIMDVectorSingle position, const SIMDVectorSingle halfExtents, const SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    return SdfTests::BoxNormal(position, halfExtents, fallback, minLengthSquared);
}

[[nodiscard]] NWB_NOINLINE SIMDVectorDouble NWB_SIMD_CALL CandidateDouble(const SIMDVectorDouble position, const SIMDVectorDouble halfExtents, const SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    return SdfDoubleTests::BoxNormal(position, halfExtents, fallback, minLengthSquared);
}

[[nodiscard]] u64 Next(u64& seed)noexcept{
    seed ^= seed << 13u;
    seed ^= seed >> 7u;
    seed ^= seed << 17u;
    return seed;
}

template<typename Scalar>
[[nodiscard]] Case<Scalar> Fill(const usize index, u64& seed)noexcept{
    using Vector = Conditional_T<IsSame_V<Scalar, f64>, SIMDVectorDouble, SIMDVectorSingle>;
    using Bits = Conditional_T<IsSame_V<Scalar, f64>, u64, u32>;
    constexpr u64 exceptional64[]{ 0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull,
        0x7fefffffffffffffull, 0xffefffffffffffffull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000142ull, 0xfff8000000000284ull, 0x7ff0000000000011ull, 0xfff0000000000022ull };
    constexpr u32 exceptional32[]{ 0u, 0x80000000u, 1u, 0x80000001u, 0x00800000u, 0x3f800000u, 0xbf800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00142u, 0xffc00284u, 0x7f800011u, 0xff800022u };
    Bits words[13];
    for(usize word = 0u; word < LengthOf(words); ++word){
        const u64 random = Next(seed);
        if constexpr(IsSame_V<Scalar, f64>)
            words[word] = index < 8192u ? exceptional64[random % LengthOf(exceptional64)] : random;
        else
            words[word] = index < 8192u ? exceptional32[random % LengthOf(exceptional32)] : static_cast<u32>(random);
    }
    Vector position;
    Vector extents;
    Vector fallback;
    for(usize lane = 0u; lane < 4u; ++lane){
        position[lane] = BitCast<Scalar>(words[lane]);
        extents[lane] = BitCast<Scalar>(words[lane + 4u]);
        fallback[lane] = BitCast<Scalar>(words[lane + 8u]);
    }
    return { position, extents, fallback, BitCast<Scalar>(words[12]) };
}

template<typename Scalar, bool Candidate>
[[nodiscard]] NWB_NOINLINE auto Run(const Case<Scalar>& input)noexcept{
    if constexpr(IsSame_V<Scalar, f64>){
        if constexpr(Candidate)
            return CandidateDouble(input.position, input.extents, input.fallback, input.threshold);
        else
            return OriginalDouble(input.position, input.extents, input.fallback, input.threshold);
    }
    else{
        if constexpr(Candidate)
            return CandidateSingle(input.position, input.extents, input.fallback, input.threshold);
        else
            return OriginalSingle(input.position, input.extents, input.fallback, input.threshold);
    }
}

template<typename Scalar>
[[nodiscard]] u32 Verify()noexcept{
#pragma STDC FENV_ACCESS ON
    using Bits = Conditional_T<IsSame_V<Scalar, f64>, u64, u32>;
    u64 seed = 0x12ace40f893d5847ull;
    u32 failures = 0u;
    for(const i32 mode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }){
        if(!SetFloatingPointRoundingMode(mode))
            return failures + 1u;
        for(usize index = 0u; index < s_CaseCount; ++index){
            const Case<Scalar> input = Fill<Scalar>(index, seed);
            if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
                return failures + 1u;
            errno = 71;
            const auto original = Run<Scalar, false>(input);
            const i32 originalFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
            const i32 originalErrno = errno;
            if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
                return failures + 1u;
            errno = 71;
            const auto candidate = Run<Scalar, true>(input);
            const i32 candidateFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
            const i32 candidateErrno = errno;
            bool equal = originalFlags == candidateFlags && originalErrno == candidateErrno;
            for(usize lane = 0u; lane < 4u; ++lane)
                equal &= BitCast<Bits>(static_cast<Scalar>(original[lane])) == BitCast<Bits>(static_cast<Scalar>(candidate[lane]));
            if(!equal && ++failures <= 8u)
                NWB_COUT << "mismatch precision=" << sizeof(Scalar) << " mode=" << mode << " index=" << index << " flags=" << originalFlags << '/' << candidateFlags << " errno=" << originalErrno << '/' << candidateErrno << '\n';
        }
    }
    return failures;
}

template<typename Scalar, bool Candidate, bool Latency>
[[nodiscard]] NWB_NOINLINE u64 Workload(const usize iterations)noexcept{
    using Vector = Conditional_T<IsSame_V<Scalar, f64>, SIMDVectorDouble, SIMDVectorSingle>;
    Case<Scalar> data[s_DataCount];
    for(usize index = 0u; index < s_DataCount; ++index){
        const Scalar x = static_cast<Scalar>((index % 7u) + 1u) * static_cast<Scalar>(0.375);
        const Scalar y = static_cast<Scalar>((index % 5u) + 1u) * static_cast<Scalar>(0.625);
        const Scalar z = static_cast<Scalar>((index % 3u) + 1u) * static_cast<Scalar>(0.875);
        data[index] = { Vector{ x, -y, z, static_cast<Scalar>(0) }, Vector{ static_cast<Scalar>(0.5), static_cast<Scalar>(0.75), static_cast<Scalar>(0.125), static_cast<Scalar>(0) },
            Vector{ static_cast<Scalar>(1), static_cast<Scalar>(0), static_cast<Scalar>(0), static_cast<Scalar>(0) }, static_cast<Scalar>(1e-12) };
    }
    Vector accumulators[8];
    for(auto& accumulator : accumulators)
        accumulator = data[0].position;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if constexpr(Latency){
            Case<Scalar> input = data[iteration % s_DataCount];
            input.position = accumulators[0];
            accumulators[0] = Run<Scalar, Candidate>(input);
        }
        else{
            for(usize lane = 0u; lane < 8u; ++lane){
                const auto value = Run<Scalar, Candidate>(data[(iteration + lane) % s_DataCount]);
                if constexpr(IsSame_V<Scalar, f64>)
                    accumulators[lane] = VectorDoubleAdd(accumulators[lane], value);
                else
                    accumulators[lane] = VectorAdd(accumulators[lane], value);
            }
        }
    }
    using Bits = Conditional_T<IsSame_V<Scalar, f64>, u64, u32>;
    u64 checksum = 0u;
    for(const auto& accumulator : accumulators){
        for(usize lane = 0u; lane < 4u; ++lane)
            checksum ^= BitCast<Bits>(static_cast<Scalar>(accumulator[lane]));
    }
    s_Checksum = checksum;
    return checksum;
}

template<typename Scalar>
[[nodiscard]] i32 Measure(const bool candidate, const bool latency, const usize iterations)noexcept{
    const Timer begin = TimerNow();
    const u64 checksum = latency ? (candidate ? Workload<Scalar, true, true>(iterations) : Workload<Scalar, false, true>(iterations)) : (candidate ? Workload<Scalar, true, false>(iterations) : Workload<Scalar, false, false>(iterations));
    const f64 seconds = DurationInNS<f64>(TimerNow(), begin) / 1.0e9;
    NWB_COUT.precision(17);
    NWB_COUT << "{\"precision\":" << sizeof(Scalar) << ",\"candidate\":" << candidate << ",\"latency\":" << latency << ",\"iterations\":" << iterations << ",\"seconds\":" << seconds << ",\"checksum\":" << checksum << "}\n";
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(const int argc, const char* const* argv){
    using namespace __hidden_box_normal_benchmark;
    if(argc == 2 && StringView(argv[1]) == "verify"){
        const auto original = HoldFloatingPointExceptions();
        if(!original)
            return 1;
        ScopeExit restore([&]()noexcept{
            if(!SetFloatingPointEnvironment(*original))
                __builtin_trap();
        });
        const u32 singleFailures = Verify<f32>();
        const u32 doubleFailures = Verify<f64>();
        NWB_COUT << "verification single=" << singleFailures << " double=" << doubleFailures << " cases=262144\n";
        return singleFailures || doubleFailures ? 1 : 0;
    }
    if(argc == 6 && StringView(argv[1]) == "measure"){
        const bool single = StringView(argv[2]) == "single";
        const bool candidate = StringView(argv[3]) == "candidate";
        const bool latency = StringView(argv[4]) == "latency";
        if((!single && StringView(argv[2]) != "double") || (!candidate && StringView(argv[3]) != "original") || (!latency && StringView(argv[4]) != "throughput"))
            return 2;
        const auto parsedIterations = ParseU64FromChars(AStringView(argv[5]));
        if(!parsedIterations || *parsedIterations == 0u || *parsedIterations > Limit<usize>::s_Max / 1u)
            return 2;
        const usize iterations = static_cast<usize>(*parsedIterations);
        return single ? Measure<f32>(candidate, latency, iterations) : Measure<f64>(candidate, latency, iterations);
    }
    NWB_CERR << "usage: simd_box_normal_benchmark verify | measure single|double original|candidate throughput|latency iterations\n";
    return 2;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

