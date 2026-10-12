// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simdmath.h>
#include <global/timer.h>

#include <cerrno>
#include <cfenv>
#include <cstdlib>
#include <iostream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_simd_optimization_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Operation{
enum Enum : u32{
    Dot2,
    Dot3,
    Dot4,
    Sum,
    RotationX,
    PerspectiveFov,
    QuaternionNormal,
    Count
};
};

inline constexpr StringView s_OperationNames[]{ "dot2", "dot3", "dot4", "sum", "rotation_x", "perspective_fov", "quaternion_normal" };
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


// Baselines retain the current reduction order and inactive-lane arithmetic contract even after production functions change.
NWB_INLINE SIMDVectorDouble BaselineDot2(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    const SIMDVectorDouble product = VectorDoubleMultiply(VectorDoubleAndInt(lhs, s_DoubleSIMDMaskXY), VectorDoubleAndInt(rhs, s_DoubleSIMDMaskXY));
    return VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
}

NWB_INLINE SIMDVectorDouble BaselineDot3(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    const SIMDVectorDouble product = VectorDoubleMultiply(VectorDoubleAndInt(lhs, s_DoubleSIMDMask3), VectorDoubleAndInt(rhs, s_DoubleSIMDMask3));
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    return VectorDoubleAdd(xy, VectorDoubleSplatZ(product));
}

NWB_INLINE SIMDVectorDouble BaselineDot4(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    const SIMDVectorDouble product = VectorDoubleMultiply(lhs, rhs);
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(product));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(product));
}

NWB_INLINE SIMDVectorDouble BaselineSum(const SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(value), VectorDoubleSplatY(value));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(value));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<bool Candidate>
NWB_INLINE SIMDMatrixDouble RotationX(const f64 angle)noexcept{
    if constexpr(Candidate)
        return MatrixDoubleRotationX(angle);
    SIMDVectorDouble sine{}, cosine{};
    VectorDoubleSinCos(sine, cosine, VectorDoubleReplicate(angle));
    SIMDMatrixDouble matrix{};
    matrix.v[0] = s_DoubleSIMDIdentityR0;
    matrix.v[1] = VectorDoubleMergeX(VectorDoubleZero(), cosine, VectorDoubleNegate(sine), VectorDoubleZero());
    matrix.v[2] = VectorDoubleMergeX(VectorDoubleZero(), sine, cosine, VectorDoubleZero());
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

template<bool Candidate>
NWB_INLINE SIMDMatrixDouble PerspectiveFov(const f64 angle)noexcept{
    if constexpr(Candidate)
        return MatrixDoublePerspectiveFovLH(angle, 1.75, 0.125, 100.0);
    SIMDVectorDouble sine{}, cosine{};
    VectorDoubleSinCos(sine, cosine, VectorDoubleScale(VectorDoubleReplicate(angle), 0.5));
    const SIMDVectorDouble zero = VectorDoubleZero(), nearZ = VectorDoubleReplicate(0.125), farZ = VectorDoubleReplicate(100.0);
    const SIMDVectorDouble height = VectorDoubleDivide(cosine, sine), width = VectorDoubleDivide(height, VectorDoubleReplicate(1.75));
    const SIMDVectorDouble range = VectorDoubleDivide(farZ, VectorDoubleSubtract(farZ, nearZ));
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleMultiply(VectorDoubleReplicate(-1.0), range), nearZ);
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(width, zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, height, zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorDoubleMergeX(zero, zero, VectorDoubleReplicate(1.0), zero);
    return matrix;
}

template<bool Candidate>
NWB_INLINE SIMDVectorDouble QuaternionNormal(const f64 angle, const SIMDVectorDouble normal = VectorDoubleSet(0.25, -0.5, 0.75, 0.0))noexcept{
    if constexpr(Candidate)
        return QuaternionDoubleRotationNormal(normal, angle);
    const SIMDVectorDouble n = VectorDoubleSelect(s_DoubleSIMDIdentityR3, normal, s_DoubleSIMDSelect1110);
    SIMDVectorDouble sine{}, cosine{};
    VectorDoubleSinCos(sine, cosine, VectorDoubleScale(VectorDoubleReplicate(angle), 0.5));
    return VectorDoubleMultiply(n, VectorDoubleSelect(cosine, sine, s_DoubleSIMDSelect1110));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<Operation::Enum Kind, bool Candidate>
NWB_INLINE SIMDVectorDouble Reduce(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    if constexpr(Kind == Operation::Dot2){
        if constexpr(Candidate) return VectorDouble2Dot(lhs, rhs);
        else return BaselineDot2(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Dot3){
        if constexpr(Candidate) return VectorDouble3Dot(lhs, rhs);
        else return BaselineDot3(lhs, rhs);
    }
    else if constexpr(Kind == Operation::Dot4){
        if constexpr(Candidate) return VectorDouble4Dot(lhs, rhs);
        else return BaselineDot4(lhs, rhs);
    }
    else{
        if constexpr(Candidate) return VectorDoubleSum(lhs);
        else return BaselineSum(lhs);
    }
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDVectorDouble Evaluate(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    return Reduce<Kind, Candidate>(lhs, rhs);
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
        if(!EqualBits(expected, actual) || expectedFlags != actualFlags || expectedErrno != actualErrno){
            if(++failures <= 3u)
                NWB_COUT << "reduction mismatch operation=" << s_OperationNames[Kind] << " case=" << index << " flags=" << expectedFlags << '/' << actualFlags << " errno=" << expectedErrno << '/' << actualErrno << '\n';
        }
    }
    return failures;
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDMatrixDouble EvaluateFactory(const f64 angle)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Kind == Operation::RotationX)
        return RotationX<Candidate>(angle);
    else if constexpr(Kind == Operation::PerspectiveFov)
        return PerspectiveFov<Candidate>(angle);
    else{
        SIMDMatrixDouble result{};
        result.v[0] = QuaternionNormal<Candidate>(angle);
        return result;
    }
}

template<Operation::Enum Kind>
u32 VerifyFactory()noexcept{
#pragma STDC FENV_ACCESS ON
    constexpr u64 bits[]{ 0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3ff0000000000000ull, 0xbff0000000000000ull, 0x7fefffffffffffffull,
        0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull, 0x400921fb54442d18ull };
    u32 failures = 0u;
    for(usize index = 0u; index < LengthOf(bits); ++index){
        const f64 angle = BitCast<f64>(bits[index]);
        if(feclearexcept(FE_ALL_EXCEPT) != 0)
            return failures + 1u;
        errno = E2BIG;
        const SIMDMatrixDouble expected = EvaluateFactory<Kind, false>(angle);
        const i32 expectedFlags = fetestexcept(FE_ALL_EXCEPT), expectedErrno = errno;
        if(feclearexcept(FE_ALL_EXCEPT) != 0)
            return failures + 1u;
        errno = E2BIG;
        const SIMDMatrixDouble actual = EvaluateFactory<Kind, true>(angle);
        const i32 actualFlags = fetestexcept(FE_ALL_EXCEPT), actualErrno = errno;
        bool equal = true;
        for(usize row = 0u; row < 4u; ++row)
            equal = equal && EqualBits(expected.v[row], actual.v[row]);
        if(!equal || expectedFlags != actualFlags || expectedErrno != actualErrno){
            if(++failures <= 3u)
                NWB_COUT << "factory mismatch operation=" << s_OperationNames[Kind] << " case=" << index << " flags=" << expectedFlags << '/' << actualFlags << " errno=" << expectedErrno << '/' << actualErrno << '\n';
        }
    }
    return failures;
}

template<bool Candidate>
NWB_NOINLINE SIMDVectorDouble EvaluateQuaternionAxis(const SIMDVectorDouble axis, const f64 angle)noexcept{
#pragma STDC FENV_ACCESS ON
    return QuaternionNormal<Candidate>(angle, axis);
}

u32 VerifyQuaternionAxes()noexcept{
#pragma STDC FENV_ACCESS ON
    constexpr u64 axisBits[][4]{
        { 0x3ff0000000000000ull, 0u, 0x8000000000000000ull, 0x7ff0000000000123ull },
        { 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull },
        { 0x7ff0000000000000ull, 0xfff0000000000000ull, 0x7fefffffffffffffull, 0u },
        { 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0u }
    };
    constexpr u64 angleBits[]{ 0u, 0x8000000000000000ull, 1u, 0x3ff0000000000000ull, 0x7ff0000000000000ull, 0x7ff8000000000233ull, 0xfff0000000000234ull };
    u32 failures = 0u;
    for(const auto& components : axisBits){
        const SIMDVectorDouble axis = VectorDoubleSetInt(components[0], components[1], components[2], components[3]);
        for(const u64 bits : angleBits){
            const f64 angle = BitCast<f64>(bits);
            if(feclearexcept(FE_ALL_EXCEPT) != 0)
                return failures + 1u;
            errno = E2BIG;
            const SIMDVectorDouble expected = EvaluateQuaternionAxis<false>(axis, angle);
            const i32 expectedFlags = fetestexcept(FE_ALL_EXCEPT), expectedErrno = errno;
            if(feclearexcept(FE_ALL_EXCEPT) != 0)
                return failures + 1u;
            errno = E2BIG;
            const SIMDVectorDouble actual = EvaluateQuaternionAxis<true>(axis, angle);
            const i32 actualFlags = fetestexcept(FE_ALL_EXCEPT), actualErrno = errno;
            if(!EqualBits(expected, actual) || expectedFlags != actualFlags || expectedErrno != actualErrno){
                if(++failures <= 3u)
                    NWB_COUT << "axis mismatch angle_bits=" << bits << " flags=" << expectedFlags << '/' << actualFlags << " errno=" << expectedErrno << '/' << actualErrno << '\n';
            }
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
        failures += VerifyReduction<Operation::Dot2>(seed);
        failures += VerifyReduction<Operation::Dot3>(seed);
        failures += VerifyReduction<Operation::Dot4>(seed);
        failures += VerifyReduction<Operation::Sum>(seed);
        failures += VerifyFactory<Operation::RotationX>();
        failures += VerifyFactory<Operation::PerspectiveFov>();
        failures += VerifyFactory<Operation::QuaternionNormal>();
        failures += VerifyQuaternionAxes();
    }
    if(fesetenv(&environment) != 0)
        ++failures;
    NWB_COUT << "verification failures=" << failures << " reduction_cases=262144 factory_cases=180 axis_cases=112\n";
    return failures;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE SIMDVectorDouble Workload(const Dataset& data, const usize iterations, const bool latency)noexcept{
    SIMDVectorDouble accumulators[8];
    for(usize lane = 0u; lane < 8u; ++lane)
        accumulators[lane] = VectorDoubleSet(0.125, 0.25, 0.5, 1.0);
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if constexpr(Kind <= Operation::Sum){
            if(latency){
                const SIMDVectorDouble input = Kind == Operation::Sum ? VectorDoubleScale(accumulators[0], 0.125) : accumulators[0];
                accumulators[0] = VectorDoubleAdd(Reduce<Kind, Candidate>(input, data.right[iteration % s_DataCount]), VectorDoubleReplicate(0.125));
            }
            else{
#pragma clang loop unroll(full)
                for(usize lane = 0u; lane < 8u; ++lane){
                    const usize index = (iteration * 8u + lane) % s_DataCount;
                    accumulators[lane] = VectorDoubleAdd(accumulators[lane], Reduce<Kind, Candidate>(data.left[index], data.right[index]));
                }
            }
        }
        else{
            const f64 angle = data.angles[iteration % s_DataCount];
            if constexpr(Kind == Operation::QuaternionNormal)
                accumulators[0] = VectorDoubleAdd(accumulators[0], QuaternionNormal<Candidate>(angle));
            else{
                const SIMDMatrixDouble matrix = Kind == Operation::RotationX ? RotationX<Candidate>(angle) : PerspectiveFov<Candidate>(angle);
                for(usize row = 0u; row < 4u; ++row)
                    accumulators[row] = VectorDoubleAdd(accumulators[row], matrix.v[row]);
            }
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
    const usize operations = Kind <= Operation::Sum && !latency ? iterations * 8u : iterations;
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
    using namespace __hidden_simd_optimization_benchmark;
    NWB_COUT.precision(17);
    if(argc == 2 && StringView(argv[1]) == "--verify")
        return Verify() == 0u ? 0 : 1;
    if(argc != 6 || StringView(argv[1]) != "--measure"){
        NWB_CERR << "usage: vector_benchmark --verify\n" << "  --measure dot2|dot3|dot4|sum baseline|candidate iterations throughput|latency\n" << "  --measure rotation_x|perspective_fov|quaternion_normal baseline|candidate iterations throughput\n";
        return 2;
    }
    const bool candidate = StringView(argv[3]) == "candidate";
    if(!candidate && StringView(argv[3]) != "baseline")
        return 2;
    const bool latency = StringView(argv[5]) == "latency";
    if(!latency && StringView(argv[5]) != "throughput")
        return 2;
    char* end = nullptr;
    const usize iterations = static_cast<usize>(strtoull(argv[4], &end, 10));
    if(iterations == 0u || end == argv[4] || *end != '\0')
        return 2;
    Dataset data{};
    InitializeDataset(data, 0x8923409102431291ull);
    for(u32 index = 0u; index < Operation::Count; ++index){
        if(StringView(argv[2]) != s_OperationNames[index])
            continue;
        if(latency && index > Operation::Sum)
            return 2;
        switch(index){
        case Operation::Dot2: SelectVariant<Operation::Dot2>(data, candidate, iterations, latency); break;
        case Operation::Dot3: SelectVariant<Operation::Dot3>(data, candidate, iterations, latency); break;
        case Operation::Dot4: SelectVariant<Operation::Dot4>(data, candidate, iterations, latency); break;
        case Operation::Sum: SelectVariant<Operation::Sum>(data, candidate, iterations, latency); break;
        case Operation::RotationX: SelectVariant<Operation::RotationX>(data, candidate, iterations, latency); break;
        case Operation::PerspectiveFov: SelectVariant<Operation::PerspectiveFov>(data, candidate, iterations, latency); break;
        case Operation::QuaternionNormal: SelectVariant<Operation::QuaternionNormal>(data, candidate, iterations, latency); break;
        default: return 2;
        }
        return 0;
    }
    return 2;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

