// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/timer.h>
#include <global/text_utils.h>
#include <global/scope_exit.h>

#include <cerrno>
#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mask_audit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Operation{
enum Enum : u32{ Move, EqualFloat, EqualDouble, EqualDoubleXYZ };
};

struct Dataset{
    SIMDVectorDouble left[64];
    SIMDVectorDouble right[64];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE u32 NWB_SIMD_CALL BaselineMoveDouble(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint64x2_t low = vshrq_n_u64(SIMDVectorDoubleDetail::GetLowBits(value), 63u);
    const uint64x2_t high = vshrq_n_u64(SIMDVectorDoubleDetail::GetHighBits(value), 63u);
    return static_cast<u32>(vgetq_lane_u64(low, 0) | (vgetq_lane_u64(low, 1) << 1u) | (vgetq_lane_u64(high, 0) << 2u) | (vgetq_lane_u64(high, 1) << 3u));
#elif defined(NWB_HAS_AVX)
    return static_cast<u32>(_mm256_movemask_pd(BitCast<__m256d>(value)));
#elif defined(NWB_HAS_SSE2)
    return static_cast<u32>(_mm_movemask_pd(SIMDVectorDoubleDetail::GetLow(value)) | (_mm_movemask_pd(SIMDVectorDoubleDetail::GetHigh(value)) << 2u));
#else
    const SIMDVectorDoubleDetail::UIntVector bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(value) >> 63u;
    return static_cast<u32>(bits[0] | (bits[1] << 1u) | (bits[2] << 2u) | (bits[3] << 3u));
#endif
}

template<bool Candidate>
NWB_INLINE u32 MoveDouble(const SIMDVectorDouble value)noexcept{
    if constexpr(Candidate)
        return VectorDoubleMoveMask(value);
    return BaselineMoveDouble(value);
}

template<bool Candidate>
NWB_INLINE bool EqualFloat4(const SIMDVectorSingle a, const SIMDVectorSingle b)noexcept{
    if constexpr(Candidate)
        return Vector4Equal(a, b);
    return (VectorMoveMask(VectorEqual(a, b)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW;
}

template<bool Candidate>
NWB_INLINE bool EqualDouble4(const SIMDVectorDouble a, const SIMDVectorDouble b)noexcept{
    if constexpr(Candidate)
        return VectorDouble4Equal(a, b);
    return (BaselineMoveDouble(VectorDoubleEqual(a, b)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW;
}

template<bool Candidate>
NWB_INLINE bool EqualDouble3(const SIMDVectorDouble a, const SIMDVectorDouble b)noexcept{
    if constexpr(Candidate)
        return VectorDouble3Equal(a, b);
    return (BaselineMoveDouble(VectorDoubleEqual(a, b)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ;
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE u32 Observe(const SIMDVectorDouble a, const SIMDVectorDouble b)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Kind == Operation::Move)
        return MoveDouble<Candidate>(a);
    else if constexpr(Kind == Operation::EqualFloat)
        return EqualFloat4<Candidate>(BitCast<SIMDVectorSingle>(__builtin_shufflevector(a, a, 0, 1)), BitCast<SIMDVectorSingle>(__builtin_shufflevector(b, b, 0, 1)));
    else if constexpr(Kind == Operation::EqualDouble)
        return EqualDouble4<Candidate>(a, b);
    else
        return EqualDouble3<Candidate>(a, b);
}

NWB_NOINLINE u32 Qualify()noexcept{
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    if(!originalEnvironment)
        return 2u;
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );
    u64 state = 0x7462825482129431ull;
    const auto next = [&]()noexcept{
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;
        return state;
    };
    for(usize sample = 0u; sample < 1000000u; ++sample){
        const SIMDVectorDouble value = VectorDoubleSetInt(next(), next(), next(), next());
        if(MoveDouble<false>(value) != MoveDouble<true>(value))
            return 1u;
    }
    constexpr u64 s_Edges[]{ 0u, 0x8000000000000000ull, 1u, 0x8000000000000001ull, 0x0010000000000000ull, 0x3FF0000000000000ull, 0xBFF0000000000000ull,
        0x7FEFFFFFFFFFFFFFull, 0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8123412341234ull, 0x7FF0234523452345ull, 0x7FC012347FC01234ull, 0x7F8012347F801234ull };
    constexpr int s_Modes[]{ FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO };
    for(const int mode : s_Modes){
        if(!SetFloatingPointRoundingMode(mode))
            return 2u;
        for(usize sample = 0u; sample < 2048u; ++sample){
            const SIMDVectorDouble a = VectorDoubleSetInt(s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)]);
            const SIMDVectorDouble b = sample % 2u ? a : VectorDoubleSetInt(s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)], s_Edges[next() % LengthOf(s_Edges)]);
#define NWB_AUDIT_COMPARE(kind) \
            if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT)) \
                return 3u; \
            errno = 0; \
            const u32 baseline##kind = Observe<Operation::kind, false>(a, b); \
            const int baselineFlags##kind = TestFloatingPointExceptions(FE_ALL_EXCEPT), baselineErrno##kind = errno; \
            if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT)) \
                return 3u; \
            errno = 0; \
            const u32 candidate##kind = Observe<Operation::kind, true>(a, b); \
            if(candidate##kind != baseline##kind || TestFloatingPointExceptions(FE_ALL_EXCEPT) != baselineFlags##kind || errno != baselineErrno##kind) \
                return 4u;
            NWB_AUDIT_COMPARE(EqualFloat)
            NWB_AUDIT_COMPARE(EqualDouble)
            NWB_AUDIT_COMPARE(EqualDoubleXYZ)
#undef NWB_AUDIT_COMPARE
        }
    }
    if(!SetFloatingPointRoundingMode(FE_TONEAREST))
        return 2u;
    return 0u;
}

NWB_NOINLINE void InitializeDataset(Dataset& dataset)noexcept{
    u64 state = 0x5629842965734193ull;
    for(usize i = 0u; i < 64u; ++i){
        const auto next = [&]()noexcept{ state ^= state << 13u; state ^= state >> 7u; state ^= state << 17u; return state; };
        dataset.left[i] = VectorDoubleSetInt(next(), next(), next(), next());
        const u64 differing = i % 3u == 0u ? 1u : 0u;
        dataset.right[i] = VectorDoubleSetInt(VectorDoubleGetIntX(dataset.left[i]) ^ differing, VectorDoubleGetIntY(dataset.left[i]), VectorDoubleGetIntZ(dataset.left[i]), VectorDoubleGetIntW(dataset.left[i]));
    }
}

template<Operation::Enum Kind, bool Candidate>
NWB_INLINE u32 Apply(const SIMDVectorDouble a, const SIMDVectorDouble b)noexcept{
    if constexpr(Kind == Operation::Move)
        return MoveDouble<Candidate>(a);
    else if constexpr(Kind == Operation::EqualFloat)
        return EqualFloat4<Candidate>(BitCast<SIMDVectorSingle>(__builtin_shufflevector(a, a, 0, 1)), BitCast<SIMDVectorSingle>(__builtin_shufflevector(b, b, 0, 1)));
    else if constexpr(Kind == Operation::EqualDouble)
        return EqualDouble4<Candidate>(a, b);
    else
        return EqualDouble3<Candidate>(a, b);
}

template<Operation::Enum Kind, bool Candidate>
NWB_NOINLINE u64 Workload(const Dataset& data, const usize iterations, const bool latency)noexcept{
    u64 results[8]{};
    if(latency){
        usize index = 1u;
        for(usize i = 0u; i < iterations; ++i){
            const u32 result = Apply<Kind, Candidate>(data.left[index], data.right[index]);
            results[0] += result;
            index = (index + result + 1u) & 63u;
        }
    }
    else{
        for(usize i = 0u; i < iterations; ++i){
            const usize index = i & 63u;
            for(usize lane = 0u; lane < 8u; ++lane)
                results[lane] += Apply<Kind, Candidate>(data.left[(index + lane * 7u) & 63u], data.right[(index + lane * 7u) & 63u]);
        }
    }
    u64 checksum = 0u;
    for(usize lane = 0u; lane < 8u; ++lane)
        checksum += results[lane];
    return checksum;
}

template<Operation::Enum Kind, bool Candidate>
void Measure(const Dataset& data, const usize iterations, const bool latency){
    const Timer start = TimerNow();
    const u64 checksum = Workload<Kind, Candidate>(data, iterations, latency);
    const f64 nanoseconds = DurationInNS<f64>(TimerNow(), start);
    const usize operations = latency ? iterations : iterations * 8u;
    const f64 nanosecondsPerOperation = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(nanoseconds), VectorDoubleReplicate(static_cast<f64>(operations))));
    NWB_COUT
        << "{\"operation\":" << Kind << ",\"variant\":\"" << (Candidate ? "candidate" : "baseline") << "\",\"workload\":\"" << (latency ? "latency" : "throughput")
        << "\",\"operations\":" << operations << ",\"nanoseconds\":" << nanoseconds << ",\"ns_per_operation\":" << nanosecondsPerOperation << ",\"checksum\":" << checksum << "}\n"
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
    using namespace __hidden_mask_audit;
    NWB_COUT.precision(17);
    if(argc == 2 && StringView(argv[1]) == "--verify"){
        const u32 status = Qualify();
        NWB_COUT << "qualification_status=" << status << '\n';
        return static_cast<int>(status);
    }
    if(argc != 6 || StringView(argv[1]) != "--measure")
        return 2;
    const bool candidate = StringView(argv[3]) == "candidate", latency = StringView(argv[5]) == "latency";
    if((!candidate && StringView(argv[3]) != "baseline") || (!latency && StringView(argv[5]) != "throughput"))
        return 2;
    const auto parsedIterations = ParseU64FromChars(AStringView(argv[4]));
    if(!parsedIterations || *parsedIterations == 0u || *parsedIterations > Limit<usize>::s_Max / (latency ? 1u : 8u))
        return 2;
    const usize iterations = static_cast<usize>(*parsedIterations);
    Dataset data{};
    InitializeDataset(data);
    if(StringView(argv[2]) == "move")
        SelectVariant<Operation::Move>(data, candidate, iterations, latency);
    else if(StringView(argv[2]) == "equal_float")
        SelectVariant<Operation::EqualFloat>(data, candidate, iterations, latency);
    else if(StringView(argv[2]) == "equal_double")
        SelectVariant<Operation::EqualDouble>(data, candidate, iterations, latency);
    else if(StringView(argv[2]) == "equal_double3")
        SelectVariant<Operation::EqualDoubleXYZ>(data, candidate, iterations, latency);
    else
        return 2;
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

